#include "PreCompile.h"
#include "RUDPSession.h"
#include "NetServerSerializeBuffer.h"
#include "MultiSocketRUDPCore.h"
#include "LogExtension.h"
#include "Logger.h"
#include "MemoryTracer.h"
#include "../Common/PacketCrypto/PacketCryptoHelper.h"

BYTE RUDPSession::maximumHoldingPacketQueueSize = 0;

RUDPSession::RUDPSession(MultiSocketRUDPCore& inCore)
	: sock(INVALID_SOCKET)
	, core(inCore)
	, flowController(maximumHoldingPacketQueueSize)
{
	ZeroMemory(recvBuffer.buffer, sizeof(recvBuffer.buffer));
	ZeroMemory(sendBuffer.rioSendBuffer, sizeof(sendBuffer.rioSendBuffer));
}

bool RUDPSession::InitializeRIO(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable, const RIO_CQ& rioRecvCQ, const RIO_CQ& rioSendCQ)
{
	u_long nonBlocking = 1;
	ioctlsocket(sock, FIONBIO, &nonBlocking);

	if (not InitRIOSendBuffer(rioFunctionTable) || not InitRIORecvBuffer(rioFunctionTable))
	{
		return false;
	}

	rioRQ = rioFunctionTable.RIOCreateRequestQueue(sock, 1, 1, 1, 1, rioRecvCQ, rioSendCQ, &sessionId);
	if (rioRQ == RIO_INVALID_RQ)
	{
		LOG_ERROR(std::format("RIOCreateRequestQueue failed with error {}", WSAGetLastError()));
		return false;
	}

	return true;
}

bool RUDPSession::InitRIOSendBuffer(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable)
{
	sendBuffer.sendBufferId = rioFunctionTable.RIORegisterBuffer(sendBuffer.rioSendBuffer, MAX_SEND_BUFFER_SIZE);
	if (sendBuffer.sendBufferId == RIO_INVALID_BUFFERID)
	{
		LOG_ERROR(std::format("RIORegisterBuffer failed with error {}", WSAGetLastError()));
		return false;
	}

	return true;
}

bool RUDPSession::InitRIORecvBuffer(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable)
{
	recvBuffer.recvContext = std::make_shared<IOContext>();
	if (recvBuffer.recvContext == nullptr)
	{
		return false;
	}

	const auto& context = recvBuffer.recvContext;
	context->InitContext(sessionId, RIO_OPERATION_TYPE::OP_RECV);
	context->Length = RECV_BUFFER_SIZE;
	context->Offset = 0;
	context->session = this;

	context->clientAddrRIOBuffer.Length = sizeof(SOCKADDR_INET);
	context->clientAddrRIOBuffer.Offset = 0;

	context->localAddrRIOBuffer.Length = sizeof(SOCKADDR_INET);
	context->localAddrRIOBuffer.Offset = 0;

	context->BufferId = rioFunctionTable.RIORegisterBuffer(recvBuffer.buffer, RECV_BUFFER_SIZE);
	context->clientAddrRIOBuffer.BufferId = rioFunctionTable.RIORegisterBuffer(context->clientAddrBuffer, sizeof(SOCKADDR_INET));
	context->localAddrRIOBuffer.BufferId = rioFunctionTable.RIORegisterBuffer(context->localAddrBuffer, sizeof(SOCKADDR_INET));

	if (context->BufferId == RIO_INVALID_BUFFERID || context->clientAddrRIOBuffer.BufferId == RIO_INVALID_BUFFERID || context->localAddrRIOBuffer.BufferId == RIO_INVALID_BUFFERID)
	{
		return false;
	}

	return true;
}

void RUDPSession::InitializeSession()
{
	sessionId = INVALID_SESSION_ID;
	ZeroMemory(sessionKey, SESSION_KEY_SIZE);
	clientAddr = {};
	clientSockAddrInet = {};
	lastSendPacketSequence = {};
	nowInReleaseThread = {};
	sessionReservedTime = {};

	if (sendBuffer.reservedSendPacketInfo != nullptr)
	{
		SendPacketInfo::Free(sendBuffer.reservedSendPacketInfo);
		sendBuffer.reservedSendPacketInfo = {};
	}

	std::scoped_lock lock(sendBuffer.sendPacketInfoQueueLock);
	const size_t sendPacketInfoQueueSize = sendBuffer.sendPacketInfoQueue.size();
	for (size_t i = 0; i < sendPacketInfoQueueSize; ++i)
	{
		const auto restItem = sendBuffer.sendPacketInfoQueue.front();
		SendPacketInfo::Free(restItem);

		sendBuffer.sendPacketInfoQueue.pop();
	}
}

RUDPSession::~RUDPSession()
{
	if (sock != INVALID_SOCKET)
	{
		closesocket(sock);
	}

	delete[] keyObjectBuffer;
}

void RUDPSession::DoDisconnect()
{
	if (auto expectReserved{ SESSION_STATE::RESERVED }; not sessionState.compare_exchange_strong(expectReserved, SESSION_STATE::RELEASING))
	{
		if (auto expectConnected{ SESSION_STATE::CONNECTED }; not sessionState.compare_exchange_strong(expectConnected, SESSION_STATE::RELEASING))
		{
			return;
		}
	}

	core.PushToDisconnectTargetSession(*this);
}

void RUDPSession::Disconnect()
{
	if (sessionState != SESSION_STATE::RELEASING)
	{
		return;
	}

	CloseSocket();
	{
		std::unique_lock lock(sendPacketInfoMapLock);
		for (const auto& item : sendPacketInfoMap | std::views::values)
		{
			core.EraseSendPacketInfo(item, threadId);
		}

		sendPacketInfoMap.clear();
	}
	OnDisconnected();

	core.DisconnectSession(sessionId);
}

bool RUDPSession::SendPacket(IPacket& packet)
{
	if (not IsConnected())
	{
		return false;
	}

	NetBuffer* buffer = NetBuffer::Alloc();
	if (buffer == nullptr)
	{
		LOG_ERROR("Buffer is nullptr in RUDPSession::SendPacket()");
		return false;
	}

	PACKET_TYPE packetType = PACKET_TYPE::SEND_TYPE;
	const PacketSequence packetSequence = ++lastSendPacketSequence;
	*buffer << packetType << packetSequence << packet.GetPacketId();
	packet.PacketToBuffer(*buffer);

	return SendPacket(*buffer, packetSequence, false, false);
}

void RUDPSession::OnConnected(const SessionIdType inSessionId)
{
	UNREFERENCED_PARAMETER(inSessionId);

	sessionState = SESSION_STATE::CONNECTED;
	OnConnected();
}

bool RUDPSession::SendPacket(NetBuffer& buffer, const PacketSequence inSendPacketSequence, const bool isReplyType, const bool isCorePacket)
{
	auto sendPacketInfo = sendPacketInfoPool->Alloc();
	if (sendPacketInfo == nullptr)
	{
		LOG_ERROR("SendPacketInfo is nullptr in RUDPSession::SendPacket()");
		NetBuffer::Free(&buffer);
		return false;
	}

	sendPacketInfo->Initialize(this, &buffer, inSendPacketSequence, isReplyType);
	if (not isReplyType)
	{
		std::unique_lock lock(sendPacketInfoMapLock);
		sendPacketInfoMap.insert({ inSendPacketSequence, sendPacketInfo });
	}

	if (buffer.m_bIsEncoded == false)
	{
		const PACKET_DIRECTION direction = isReplyType ? PACKET_DIRECTION::SERVER_TO_CLIENT_REPLY : PACKET_DIRECTION::SERVER_TO_CLIENT;
		PacketCryptoHelper::EncodePacket(
			buffer,
			inSendPacketSequence,
			direction,
			sessionSalt,
			SESSION_SALT_SIZE,
			sessionKeyHandle,
			isCorePacket
		);
	}

	if (not core.SendPacket(sendPacketInfo))
	{
		SendPacketInfo::Free(sendPacketInfo);
		if (not isReplyType)
		{
			std::unique_lock lock(sendPacketInfoMapLock);
			sendPacketInfoMap.erase(inSendPacketSequence);
		}

		return false;
	}

	SendPacketInfo::Free(sendPacketInfo);
	return true;
}

void RUDPSession::SendHeartbeatPacket()
{
	NetBuffer& buffer = *NetBuffer::Alloc();

	auto packetType = PACKET_TYPE::HEARTBEAT_TYPE;
	const PacketSequence packetSequence = ++lastSendPacketSequence;
	buffer << packetType << packetSequence;

	std::ignore = SendPacket(buffer, packetSequence, false, true);
}

bool RUDPSession::CheckReservedSessionTimeout(const unsigned long long now) const
{
	return (sessionState.load() == SESSION_STATE::RESERVED) && (now - sessionReservedTime >= RESERVED_SESSION_TIMEOUT_MS);
}

void RUDPSession::AbortReservedSession()
{
	if (auto connectState{ SESSION_STATE::RESERVED }; not sessionState.compare_exchange_strong(connectState, SESSION_STATE::RELEASING))
	{
		return;
	}

	CloseSocket();
	core.DisconnectSession(sessionId);
}

void RUDPSession::CloseSocket()
{
	if (sock == INVALID_SOCKET)
	{
		return;
	}

	std::unique_lock lock(socketLock);
	{
		closesocket(sock);
		UnregisterRIOBuffers();
		sock = INVALID_SOCKET;
	}
}

void RUDPSession::UnregisterRIOBuffers()
{
	const auto& rioFunctionTable = core.GetRIOFunctionTable();
	if (sendBuffer.sendBufferId != RIO_INVALID_BUFFERID)
	{
		rioFunctionTable.RIODeregisterBuffer(sendBuffer.sendBufferId);
		sendBuffer.sendBufferId = RIO_INVALID_BUFFERID;
	}
	if (recvBuffer.recvContext != nullptr)
	{
		const auto& context = recvBuffer.recvContext;
		if (context->BufferId != RIO_INVALID_BUFFERID)
		{
			rioFunctionTable.RIODeregisterBuffer(context->BufferId);
			context->BufferId = RIO_INVALID_BUFFERID;
		}
		if (context->clientAddrRIOBuffer.BufferId != RIO_INVALID_BUFFERID)
		{
			rioFunctionTable.RIODeregisterBuffer(context->clientAddrRIOBuffer.BufferId);
			context->clientAddrRIOBuffer.BufferId = RIO_INVALID_BUFFERID;
		}
		if (context->localAddrRIOBuffer.BufferId != RIO_INVALID_BUFFERID)
		{
			rioFunctionTable.RIODeregisterBuffer(context->localAddrRIOBuffer.BufferId);
			context->localAddrRIOBuffer.BufferId = RIO_INVALID_BUFFERID;
		}
	}
}

void RUDPSession::SetMaximumPacketHoldingQueueSize(const BYTE size)
{
	maximumHoldingPacketQueueSize = size;
}

void RUDPSession::TryConnect(NetBuffer& recvPacket, const sockaddr_in& inClientAddr)
{
	PacketSequence packetSequence;
	SessionIdType recvSessionId;

	recvPacket >> packetSequence >> recvSessionId;
	if (packetSequence != LOGIN_PACKET_SEQUENCE || sessionId != recvSessionId)
	{
		return;
	}

	if (auto connectState{ SESSION_STATE::RESERVED }; not sessionState.compare_exchange_strong(connectState, SESSION_STATE::CONNECTED))
	{
		return;
	}
	clientAddr = inClientAddr;
	memset(&clientSockAddrInet, 0, sizeof(clientSockAddrInet));
	clientSockAddrInet.Ipv4 = inClientAddr;
	++nextRecvPacketSequence;

	OnConnected(sessionId);
	SendReplyToClient(packetSequence);
}

void RUDPSession::Disconnect(NetBuffer& recvPacket)
{
	DoDisconnect();
}

bool RUDPSession::OnRecvPacket(NetBuffer& recvPacket)
{
	PacketSequence packetSequence;
	recvPacket >> packetSequence;

	if (nextRecvPacketSequence != packetSequence)
	{
		if (packetSequence < nextRecvPacketSequence)
		{
			SendReplyToClient(packetSequence);
		}
		else if (nextRecvPacketSequence < packetSequence && not recvHoldingPacketSequences.contains(packetSequence))
		{
			NetBuffer::AddRefCount(&recvPacket);
			recvPacketHolderQueue.emplace(&recvPacket, packetSequence);
			recvHoldingPacketSequences.emplace(packetSequence);
		}

		return true;
	}

	if (ProcessPacket(recvPacket, packetSequence) == false)
	{
		return false;
	}

	return ProcessHoldingPacket();
}

bool RUDPSession::ProcessHoldingPacket()
{
	PacketSequence packetSequence;

	while (not recvPacketHolderQueue.empty())
	{
		NetBuffer* storedBuffer;
		{
			auto& recvPacketHolderTop = recvPacketHolderQueue.top();
			if (recvPacketHolderTop.packetSequence > nextRecvPacketSequence)
			{
				break;
			}

			packetSequence = recvPacketHolderTop.packetSequence;
			storedBuffer = recvPacketHolderTop.buffer;
			recvPacketHolderQueue.pop();
		}
		
		if (ProcessPacket(*storedBuffer, packetSequence) == false)
		{
			return false;
		}

		NetBuffer::Free(storedBuffer);
	}
	
	return true;
}

bool RUDPSession::ProcessPacket(NetBuffer& recvPacket, const PacketSequence recvPacketSequence)
{
	recvHoldingPacketSequences.erase(recvPacketSequence);
	++nextRecvPacketSequence;

	PacketId packetId;
	recvPacket >> packetId;

	auto const itor = packetFactoryMap.find(packetId);
	if (itor == packetFactoryMap.end())
	{
		return false;
	}

	if (not itor->second(this, &recvPacket)())
	{
		return false;
	}

	SendReplyToClient(recvPacketSequence);
	return true;
}

void RUDPSession::SendReplyToClient(const PacketSequence recvPacketSequence)
{
	NetBuffer& buffer = *NetBuffer::Alloc();

	auto packetType = PACKET_TYPE::SEND_REPLY_TYPE;
	buffer << packetType << recvPacketSequence;

	std::ignore = SendPacket(buffer, recvPacketSequence, true, true);
}

void RUDPSession::OnSendReply(NetBuffer& recvPacket)
{
	PacketSequence packetSequence;
	recvPacket >> packetSequence;

	if (lastSendPacketSequence < packetSequence)
	{
		return;
	}

	SendPacketInfo* sendPacketInfo;
	{
		std::unique_lock lock(sendPacketInfoMapLock);
		if (const auto itor = sendPacketInfoMap.find(packetSequence); itor != sendPacketInfoMap.end())
		{
			sendPacketInfo = itor->second;
		}
		else
		{
			return;
		}

		sendPacketInfoMap.erase(packetSequence);
	}

	core.EraseSendPacketInfo(sendPacketInfo, threadId);
}

SessionIdType RUDPSession::GetSessionId() const
{
	return sessionId;
}

sockaddr_in RUDPSession::GetSocketAddress() const
{
	return clientAddr;
}

SOCKADDR_INET RUDPSession::GetSocketAddressInet() const
{
	return clientSockAddrInet;
}

bool RUDPSession::CanProcessPacket(const sockaddr_in& targetClientAddr) const
{
	return CheckMyClient(clientAddr) && not IsReleasing();
}

bool RUDPSession::CheckMyClient(const sockaddr_in& targetClientAddr) const
{
	if (clientAddr.sin_addr.S_un.S_addr != targetClientAddr.sin_addr.S_un.S_addr ||
		clientAddr.sin_port != targetClientAddr.sin_port)
	{
		return false;
	}

	return true;
}

bool RUDPSession::IsReleasing() const
{
	return nowInReleaseThread;
}
