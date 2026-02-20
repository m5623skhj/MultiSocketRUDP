#include "PreCompile.h"
#include "RUDPSession.h"
#include "NetServerSerializeBuffer.h"
#include "MultiSocketRUDPCore.h"
#include "LogExtension.h"
#include "Logger.h"
#include "MemoryTracer.h"
#include "IOContext.h"
#include "SendPacketInfo.h"
#include "../Common/PacketCrypto/PacketCryptoHelper.h"

BYTE RUDPSession::maximumHoldingPacketQueueSize = 0;

RUDPSession::RUDPSession(MultiSocketRUDPCore& inCore)
	: sock(INVALID_SOCKET)
	, recvBuffer()
	, flowManager(maximumHoldingPacketQueueSize)
	, sessionPacketOrderer(maximumHoldingPacketQueueSize)
	, core(inCore)
{
	ZeroMemory(recvBuffer.buffer, sizeof(recvBuffer.buffer));
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
	const auto bufferId = rioFunctionTable.RIORegisterBuffer(sendContext.GetRIOSendBuffer(), MAX_SEND_BUFFER_SIZE);
	if (bufferId == RIO_INVALID_BUFFERID)
	{
		LOG_ERROR(std::format("RIORegisterBuffer failed with error {}", WSAGetLastError()));
		return false;
	}

	sendContext.SetSendRIOBufferId(bufferId);

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
	cryptoContext.Initialize();
	clientAddr = {};
	clientSockAddrInet = {};
	nowInReleaseThread = {};
	sessionReservedTime = {};

	flowManager.Initialize(maximumHoldingPacketQueueSize);
	sendContext.Reset();
	sessionPacketOrderer.Initialize(maximumHoldingPacketQueueSize);
}

void RUDPSession::SetSessionId(const SessionIdType inSessionId)
{
	sessionId = inSessionId;
}

void RUDPSession::SetThreadId(const ThreadIdType inThreadId)
{
	threadId = inThreadId;
}

RUDPSession::~RUDPSession()
{
	if (sock != INVALID_SOCKET)
	{
		closesocket(sock);
	}
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

	OnDisconnected();
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
		sendContext.ForEachAndClearSendPacketInfoMap([this](SendPacketInfo* info)
		{
			core.EraseSendPacketInfo(info, threadId);
		});
	}
	OnReleased();

	core.DisconnectSession(sessionId);
}

bool RUDPSession::SendPacket(IPacket& packet)
{
	if (not IsConnected())
	{
		return false;
	}

	if (const PacketSequence nextSequence = sendContext.GetLastSendPacketSequence() + 1; not flowManager.CanSend(nextSequence))
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
	const PacketSequence packetSequence = sendContext.IncrementLastSendPacketSequence();
	*buffer << packetType << packetSequence << packet.GetPacketId();
	packet.PacketToBuffer(*buffer);

	return SendPacket(*buffer, packetSequence, false, false);
}

ThreadIdType RUDPSession::GetThreadId() const
{
	return threadId;
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
		sendContext.InsertSendPacketInfo(inSendPacketSequence, sendPacketInfo);
	}

	if (buffer.m_bIsEncoded == false)
	{
		const PACKET_DIRECTION direction = isReplyType ? PACKET_DIRECTION::SERVER_TO_CLIENT_REPLY : PACKET_DIRECTION::SERVER_TO_CLIENT;
		PacketCryptoHelper::EncodePacket(
			buffer,
			inSendPacketSequence,
			direction,
			cryptoContext.GetSessionSalt(),
			SESSION_SALT_SIZE,
			cryptoContext.GetSessionKeyHandle(),
			isCorePacket
		);
	}

	if (not core.SendPacket(sendPacketInfo))
	{
		SendPacketInfo::Free(sendPacketInfo);
		if (not isReplyType)
		{
			sendContext.EraseSendPacketInfo(inSendPacketSequence);
		}

		return false;
	}

	SendPacketInfo::Free(sendPacketInfo);
	return true;
}

void RUDPSession::SendHeartbeatPacket()
{
	if (const PacketSequence nextSequence = sendContext.GetLastSendPacketSequence() + 1; not flowManager.CanSend(nextSequence))
	{
		return;
	}

	NetBuffer& buffer = *NetBuffer::Alloc();

	auto packetType = PACKET_TYPE::HEARTBEAT_TYPE;
	const PacketSequence packetSequence = sendContext.IncrementLastSendPacketSequence();
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

void RUDPSession::UnregisterRIOBuffer(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable, OUT RIO_BUFFERID& bufferId)
{
	if (bufferId != RIO_INVALID_BUFFERID)
	{
		rioFunctionTable.RIODeregisterBuffer(bufferId);
		bufferId = RIO_INVALID_BUFFERID;
	}
}

void RUDPSession::UnregisterRIOBuffers()
{
	const auto& rioFunctionTable = core.GetRIOFunctionTable();
	RIO_BUFFERID sendBufferId = sendContext.GetSendBufferId();
	UnregisterRIOBuffer(rioFunctionTable, sendBufferId);
	sendContext.SetSendRIOBufferId(RIO_INVALID_BUFFERID);

	if (recvBuffer.recvContext != nullptr)
	{
		const auto& context = recvBuffer.recvContext;
		UnregisterRIOBuffer(rioFunctionTable, context->BufferId);
		UnregisterRIOBuffer(rioFunctionTable, context->clientAddrRIOBuffer.BufferId);
		UnregisterRIOBuffer(rioFunctionTable, context->localAddrRIOBuffer.BufferId);

		context->BufferId = RIO_INVALID_BUFFERID;
		context->clientAddrRIOBuffer.BufferId = RIO_INVALID_BUFFERID;
		context->localAddrRIOBuffer.BufferId = RIO_INVALID_BUFFERID;
	}
}

void RUDPSession::SetMaximumPacketHoldingQueueSize(const BYTE size)
{
	maximumHoldingPacketQueueSize = size;
}

void RUDPSession::EnqueueToRecvBufferList(NetBuffer* buffer)
{
	recvBuffer.recvBufferList.Enqueue(buffer);
}

RecvBuffer& RUDPSession::GetRecvBuffer()
{
	return recvBuffer;
}

std::shared_ptr<IOContext> RUDPSession::GetRecvBufferContext() const
{
	return recvBuffer.recvContext;
}

void RUDPSession::RecvContextReset()
{
	if (recvBuffer.recvContext == nullptr)
	{
		return;
	}

	recvBuffer.recvContext.reset();
}

RIO_RQ RUDPSession::GetRecvRIORQ() const
{
	return rioRQ;
}

RIO_RQ RUDPSession::GetSendRIORQ() const
{
	return rioRQ;
}

bool RUDPSession::TryConnect(NetBuffer& recvPacket, const sockaddr_in& inClientAddr)
{
	PacketSequence packetSequence;
	SessionIdType recvSessionId;

	recvPacket >> packetSequence >> recvSessionId;
	if (packetSequence != LOGIN_PACKET_SEQUENCE || sessionId != recvSessionId)
	{
		return false;
	}

	if (auto connectState{ SESSION_STATE::RESERVED }; not sessionState.compare_exchange_strong(connectState, SESSION_STATE::CONNECTED))
	{
		return false;
	}

	clientAddr = inClientAddr;
	memset(&clientSockAddrInet, 0, sizeof(clientSockAddrInet));
	clientSockAddrInet.Ipv4 = inClientAddr;

	constexpr PacketSequence startSequence = LOGIN_PACKET_SEQUENCE + 1;
	sessionPacketOrderer.Reset(startSequence);
	flowManager.Reset(startSequence);

	OnConnected(sessionId);
	SendReplyToClient(packetSequence);

	return true;
}

void RUDPSession::Disconnect(NetBuffer& recvPacket)
{
	DoDisconnect();
}

bool RUDPSession::OnRecvPacket(NetBuffer& recvPacket)
{
	PacketSequence packetSequence;
	recvPacket >> packetSequence;

	if (not flowManager.CanAccept(packetSequence))
	{
		return true;
	}

	const auto packetProcessResult = sessionPacketOrderer.OnReceive(
		packetSequence,
		recvPacket,
		[this](NetBuffer& buffer, const PacketSequence sequence) -> bool
		{
			return ProcessPacket(buffer, sequence);
		});

	switch (packetProcessResult)
	{
	case ON_RECV_RESULT::DUPLICATED_RECV:
	{
		SendReplyToClient(packetSequence);
		[[fallthrough]];
	}
	case ON_RECV_RESULT::PROCESSED:
	case ON_RECV_RESULT::PACKET_HELD:
	{
		return true;
	}
	case ON_RECV_RESULT::ERROR_OCCURED:
	{
		return false;
	}
	}

	return false;
}

bool RUDPSession::ProcessPacket(NetBuffer& recvPacket, const PacketSequence recvPacketSequence)
{
	PacketId packetId;
	recvPacket >> packetId;

	auto const itor = packetFactoryMap.find(packetId);
	if (itor == packetFactoryMap.end())
	{
		LOG_ERROR(std::format("Received unknown packet. packetId: {}", packetId));
		return false;
	}

	if (not itor->second(this, &recvPacket)())
	{
		LOG_ERROR(std::format("Failed to process received packet. packetId: {}", packetId));
		return false;
	}

	flowManager.MarkReceived(recvPacketSequence);
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

	if (sendContext.GetLastSendPacketSequence() < packetSequence)
	{
		return;
	}

	SendPacketInfo* sendPacketInfo = sendContext.FindAndEraseSendPacketInfo(packetSequence);
	if (sendPacketInfo == nullptr)
	{
		return;
	}

	flowManager.OnAckReceived(packetSequence);
	core.EraseSendPacketInfo(sendPacketInfo, threadId);
}

std::shared_mutex& RUDPSession::GetSocketMutex() const
{
	return socketLock;
}

SessionIdType RUDPSession::GetSessionId() const
{
	return sessionId;
}

SOCKET RUDPSession::GetSocket() const
{
	return sock;
}

sockaddr_in RUDPSession::GetSocketAddress() const
{
	return clientAddr;
}

SOCKADDR_INET RUDPSession::GetSocketAddressInet() const
{
	return clientSockAddrInet;
}

SOCKADDR_INET& RUDPSession::GetSocketAddressInetRef()
{
	return clientSockAddrInet;
}

bool RUDPSession::IsConnected() const
{
	return sessionState == SESSION_STATE::CONNECTED;
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

bool RUDPSession::IsReserved() const
{
	return sessionState == SESSION_STATE::RESERVED;
}

bool RUDPSession::IsUsingSession() const
{
	return sessionState == SESSION_STATE::RESERVED || sessionState == SESSION_STATE::CONNECTED;
}

SESSION_STATE RUDPSession::GetSessionState() const
{
	return sessionState.load();
}

bool RUDPSession::IsReleasing() const
{
	return nowInReleaseThread;
}

SessionCryptoContext& RUDPSession::GetCryptoContext()
{
	return cryptoContext;
}

const SessionCryptoContext& RUDPSession::GetCryptoContext() const
{
	return cryptoContext;
}

SessionSendContext& RUDPSession::GetSendContext()
{
	return sendContext;
}

const SessionSendContext& RUDPSession::GetSendContext() const
{
	return sendContext;
}