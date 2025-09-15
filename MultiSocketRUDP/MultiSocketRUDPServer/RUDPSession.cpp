#include "PreCompile.h"
#include "RUDPSession.h"
#include "NetServerSerializeBuffer.h"
#include "MultiSocketRUDPCore.h"
#include "LogExtension.h"
#include "Logger.h"

RUDPSession::RUDPSession(MultiSocketRUDPCore& inCore)
	: sock(INVALID_SOCKET)
	, serverPort(INVALID_PORT_NUMBER)
	, core(inCore)
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
	sessionKey = {};
	clientAddr = {};
	clientSockAddrInet = {};
	lastSendPacketSequence = {};
	nowInReleaseThread = {};

	if (sendBuffer.reservedSendPacketInfo != nullptr)
	{
		SendPacketInfo::Free(sendBuffer.reservedSendPacketInfo);
		sendBuffer.reservedSendPacketInfo = {};
	}

	SendPacketInfo* eraseTarget = nullptr;
	std::scoped_lock lock(sendBuffer.sendPacketInfoQueueLock);
	const int sendPacketInfoQueueSize = sendBuffer.sendPacketInfoQueue.size();
	for (int i = 0; i < sendPacketInfoQueueSize; ++i)
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
}

void RUDPSession::Disconnect()
{
	if (bool connectState{ true }; not isConnected.compare_exchange_strong(connectState, false))
	{
		return;
	}

	{
		std::unique_lock lock(sendPacketInfoMapLock);
		for (const auto& item : sendPacketInfoMap | std::views::values)
		{
			core.EraseSendPacketInfo(item, threadId);
		}
	}
	CloseSocket();
	OnDisconnected();

	core.DisconnectSession(sessionId);
}

bool RUDPSession::SendPacket(IPacket& packet)
{
	if (not isConnected)
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

	return SendPacket(*buffer, packetSequence, false);
}

void RUDPSession::OnConnected(const SessionIdType inSessionId)
{
	sessionId = inSessionId;
	OnConnected();
}

bool RUDPSession::SendPacket(NetBuffer& buffer, const PacketSequence inSendPacketSequence, const bool isReplyType)
{
	auto sendPacketInfo = sendPacketInfoPool->Alloc();
	if (sendPacketInfo == nullptr)
	{
		LOG_ERROR("SendPacketInfo is nullptr in RUDPSession::SendPacket()");
		NetBuffer::Free(&buffer);
		return false;
	}

	sendPacketInfo->Initialize(this, &buffer, inSendPacketSequence, isReplyType);
	if (isReplyType == false)
	{
		sendPacketInfo->AddRefCount();

		std::unique_lock lock(sendPacketInfoMapLock);
		sendPacketInfoMap.insert({ inSendPacketSequence, sendPacketInfo });
	}

	if (not core.SendPacket(sendPacketInfo))
	{
		SendPacketInfo::Free(sendPacketInfo);

		std::unique_lock lock(sendPacketInfoMapLock);
		sendPacketInfoMap.erase(inSendPacketSequence);

		return false;
	}

	return true;
}

void RUDPSession::SendHeartbeatPacket()
{
	NetBuffer& buffer = *NetBuffer::Alloc();

	auto packetType = PACKET_TYPE::HEARTBEAT_TYPE;
	const PacketSequence packetSequence = ++lastSendPacketSequence;
	{
		std::string str = "SendHeartbeatPacketSequence : " + std::to_string(packetSequence) + '\n';
		std::cout << str;
	}
	buffer << packetType << packetSequence;

	std::ignore = SendPacket(buffer, packetSequence, false);
}

void RUDPSession::CloseSocket()
{
	if (sock == INVALID_SOCKET)
	{
		return;
	}

	closesocket(sock);
	sock = INVALID_SOCKET;
}

void RUDPSession::TryConnect(NetBuffer& recvPacket, const sockaddr_in& inClientAddr)
{
	PacketSequence packetSequence;
	SessionIdType inputSessionId;
	std::string inputSessionKey;

	recvPacket >> packetSequence >> inputSessionId >> inputSessionKey;
	if (packetSequence != LOGIN_PACKET_SEQUENCE || sessionId != inputSessionId || sessionKey != inputSessionKey)
	{
		return;
	}

	if (bool connectState{ false }; not isConnected.compare_exchange_strong(connectState, true))
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
	Disconnect();
}

bool RUDPSession::OnRecvPacket(NetBuffer& recvPacket)
{
	PacketSequence packetSequence;
	recvPacket >> packetSequence;

	if (nextRecvPacketSequence != packetSequence)
	{
		if (nextRecvPacketSequence < packetSequence && not recvHoldingPacketSequences.contains(packetSequence))
		{
			NetBuffer::AddRefCount(&recvPacket);
			recvPacketHolderQueue.emplace(&recvPacket, packetSequence);
			recvHoldingPacketSequences.emplace(packetSequence);
		}
	}
	else if (ProcessPacket(recvPacket, packetSequence) == false)
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
			if (recvPacketHolderTop.packetSequence <= nextRecvPacketSequence)
			{
				recvPacketHolderQueue.pop();
				recvHoldingPacketSequences.erase(recvPacketHolderTop.packetSequence);
				continue;
			}

			if (recvPacketHolderTop.packetSequence > nextRecvPacketSequence)
			{
				break;
			}

			packetSequence = recvPacketHolderTop.packetSequence;
			storedBuffer = recvPacketHolderTop.buffer;
			recvPacketHolderQueue.pop();
		}
		
		if (ProcessPacket(*storedBuffer, packetSequence, false) == false)
		{
			return false;
		}
	}
	
	return true;
}

bool RUDPSession::ProcessPacket(NetBuffer& recvPacket, const PacketSequence recvPacketSequence, const bool needReplyToClient)
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

	std::ignore = SendPacket(buffer, recvPacketSequence, true);
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

bool RUDPSession::IsConnected() const
{
	return isConnected;
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