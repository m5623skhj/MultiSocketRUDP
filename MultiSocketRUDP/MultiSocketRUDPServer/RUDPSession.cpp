#include "PreCompile.h"
#include "RUDPSession.h"
#include "NetServerSerializeBuffer.h"
#include "MultiSocketRUDPCore.h"
#include "LogExtension.h"
#include "Logger.h"
#include "MemoryTracer.h"
#include "IOContext.h"
#include "MultiSocketRUDPCoreFuntionDeletage.h"
#include "SendPacketInfo.h"
#include "../Common/PacketCrypto/PacketCryptoHelper.h"

BYTE RUDPSession::maximumHoldingPacketQueueSize = 0;

RUDPSession::RUDPSession(MultiSocketRUDPCore& inCore)
	: flowManager(maximumHoldingPacketQueueSize)
	, sessionPacketOrderer(maximumHoldingPacketQueueSize)
	, rioContext()
	, core(inCore)
{
}

bool RUDPSession::InitializeRIO(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable, const RIO_CQ& rioRecvCQ, const RIO_CQ& rioSendCQ)
{
	return rioContext.Initialize(
		rioFunctionTable, 
		rioRecvCQ, 
		rioSendCQ, 
		socketContext.GetSocket(), 
		sessionId, 
		this,
		maximumHoldingPacketQueueSize);
}

void RUDPSession::InitializeSession()
{
	sessionGeneration.fetch_add(1, std::memory_order_release);

	cryptoContext.Initialize();
	clientAddr = {};
	clientSockAddrInet = {};
	nowInReleaseThread.store(false, std::memory_order_release);
	sessionReservedTime = {};
	onSessionReleaseTime = {};

	flowManager.Initialize(maximumHoldingPacketQueueSize);
	rioContext.GetSendContext().Reset();
	sessionPacketOrderer.Initialize(maximumHoldingPacketQueueSize);
	disconnectedReason = DISCONNECT_REASON::NOT_DISCONNECTED;

	stateMachine.SetDisconnected();
}

void RUDPSession::SetSessionId(const SessionIdType inSessionId)
{
	sessionId = inSessionId;
}

void RUDPSession::SetThreadId(const ThreadIdType inThreadId)
{
	threadId = inThreadId;
}

void RUDPSession::DoDisconnect(const DISCONNECT_REASON inDisconnectSession)
{
	if (not stateMachine.TryTransitionToReleasing())
	{
		return;
	}

	disconnectedReason = inDisconnectSession;
	nowInReleaseThread.store(true, std::memory_order_seq_cst);
	OnDisconnected();
	MultiSocketRUDPCoreFunctionDelegate::PushToDisconnectTargetSession(*this);
}

void RUDPSession::Disconnect()
{
	if (not stateMachine.IsReleasing())
	{
		return;
	}

	CloseSocket();
	{
		rioContext.GetSendContext().ForEachAndClearSendPacketInfoMap([this](SendPacketInfo* info)
		{
			core.EraseSendPacketInfo(info, threadId);
		});
	}
	OnReleased();

	const SessionIdType disconnectTargetSessionId = sessionId;
	MultiSocketRUDPCoreFunctionDelegate::DisconnectSession(disconnectTargetSessionId);
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
		DoDisconnect(DISCONNECT_REASON::BY_ERROR);
		return false;
	}

	PACKET_TYPE packetType = PACKET_TYPE::SEND_TYPE;
	const PacketSequence packetSequence = rioContext.GetSendContext().IncrementLastSendPacketSequence();
	*buffer << packetType << packetSequence << packet.GetPacketId();
	packet.PacketToBuffer(*buffer);

	if (not SendPacket(*buffer, packetSequence, false, false))
	{
		DoDisconnect(DISCONNECT_REASON::BY_ERROR);
		return false;
	}

	return true;
}

ThreadIdType RUDPSession::GetThreadId() const
{
	return threadId;
}

void RUDPSession::OnConnected(const SessionIdType inSessionId)
{
	UNREFERENCED_PARAMETER(inSessionId);

	OnConnected();
}

bool RUDPSession::SendPacket(NetBuffer& buffer, const PacketSequence inSendPacketSequence, const bool isReplyType, const bool isCorePacket)
{
	if (not isReplyType)
	{
		std::scoped_lock lock(rioContext.GetSendContext().GetPendingQueueLock());

		if (not rioContext.GetSendContext().IsPendingQueueEmpty() || not flowManager.CanSend(inSendPacketSequence))
		{
			if (not rioContext.GetSendContext().PushToPendingQueue(inSendPacketSequence, &buffer))
			{
				LOG_ERROR("Pending queue is full in RUDPSession::SendPacket()");
				NetBuffer::Free(&buffer);
				return false;
			}

			return true;
		}
	}

	return SendPacketImmediate(buffer, inSendPacketSequence, isReplyType, isCorePacket);
}

bool RUDPSession::SendPacketImmediate(
	NetBuffer& buffer, 
	const PacketSequence inSendPacketSequence,
	const bool isReplyType, 
	const bool isCorePacket)
{
	const auto sendPacketInfo = sendPacketInfoPool->Alloc();
	if (sendPacketInfo == nullptr)
	{
		LOG_ERROR("SendPacketInfo is nullptr in RUDPSession::SendPacketImmediate()");
		NetBuffer::Free(&buffer);
		return false;
	}

	sendPacketInfo->Initialize(this, sessionGeneration.load(std::memory_order_acquire), &buffer, inSendPacketSequence, isReplyType);
	if (not isReplyType)
	{
		rioContext.GetSendContext().InsertSendPacketInfo(inSendPacketSequence, sendPacketInfo);
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
		if (not isReplyType)
		{
			core.EraseSendPacketInfo(sendPacketInfo, threadId);
			rioContext.GetSendContext().EraseSendPacketInfo(inSendPacketSequence);
		}
		else
		{
			sendPacketInfo->isErasedPacketInfo.store(true, std::memory_order_release);
			SendPacketInfo::Free(sendPacketInfo);
		}

		return false;
	}

	SendPacketInfo::Free(sendPacketInfo);
	return true;
}

void RUDPSession::TryFlushPendingQueue()
{
	std::vector<std::pair<PacketSequence, NetBuffer*>> sendBuffers;
	{
		std::scoped_lock lock(rioContext.GetSendContext().GetPendingQueueLock());
		while (not rioContext.GetSendContext().IsPendingQueueEmpty())
		{
			if (const auto& [sequence, _] = rioContext.GetSendContext().PendingQueueFront(); not flowManager.CanSend(sequence))
			{
				break;
			}

			std::pair<PacketSequence, NetBuffer*> item;
			rioContext.GetSendContext().PopFromPendingQueue(item);
			sendBuffers.push_back(item);
		}
	}

	size_t bufferIndex = 0;
	const size_t sendBuffersSize = sendBuffers.size();
	for (; bufferIndex < sendBuffersSize; ++bufferIndex)
	{
		auto& [packetSequence, buffer] = sendBuffers[bufferIndex];
		if (not SendPacketImmediate(*buffer, packetSequence, false, false))
		{
			DoDisconnect(DISCONNECT_REASON::BY_ERROR);
			++bufferIndex;
			break;
		}
	}

	for (; bufferIndex < sendBuffersSize; ++bufferIndex)
	{
		NetBuffer::Free(sendBuffers[bufferIndex].second);
	}
}

void RUDPSession::SendHeartbeatPacket()
{
	if (nowInReleaseThread.load(std::memory_order_acquire) || not IsConnected())
	{
		return;
	}

	if (const PacketSequence nextSequence = rioContext.GetSendContext().GetLastSendPacketSequence() + 1; not flowManager.CanSend(nextSequence))
	{
		return;
	}

	NetBuffer* buffer = NetBuffer::Alloc();
	if (buffer == nullptr)
	{
		LOG_ERROR("Buffer is nullptr in RUDPSession::SendHeartbeatPacket()");
		DoDisconnect(DISCONNECT_REASON::BY_ERROR);
		return;
	}

	auto packetType = PACKET_TYPE::HEARTBEAT_TYPE;
	const PacketSequence packetSequence = rioContext.GetSendContext().IncrementLastSendPacketSequence();
	*buffer << packetType << packetSequence;

	if (not SendPacket(*buffer, packetSequence, false, true))
	{
		DoDisconnect(DISCONNECT_REASON::BY_ERROR);
	}
}

bool RUDPSession::CheckReservedSessionTimeout(const unsigned long long now) const
{
	return stateMachine.IsReserved() && (now - sessionReservedTime >= RESERVED_SESSION_TIMEOUT_MS);
}

void RUDPSession::AbortReservedSession()
{
	if (not stateMachine.TryAbortReserved())
	{
		return;
	}

	const SessionIdType disconnectTargetSessionId = sessionId;
	nowInReleaseThread.store(true, std::memory_order_seq_cst);
	disconnectedReason = DISCONNECT_REASON::BY_ABORT_RESERVED;
	CloseSocket();
	MultiSocketRUDPCoreFunctionDelegate::DisconnectSession(disconnectTargetSessionId);
}

void RUDPSession::CloseSocket()
{
	rioContext.Cleanup(core.GetRIOFunctionTable());
	socketContext.CloseSocket();
}

void RUDPSession::SetMaximumPacketHoldingQueueSize(const BYTE size)
{
	maximumHoldingPacketQueueSize = size;
}

void RUDPSession::EnqueueToRecvBufferList(NetBuffer* buffer)
{
	rioContext.EnqueueToRecvBufferList(buffer);
}

RecvBuffer& RUDPSession::GetRecvBuffer()
{
	return rioContext.GetRecvBuffer();
}

std::shared_ptr<IOContext> RUDPSession::GetRecvBufferContext() const
{
	return rioContext.GetRecvBufferContext();
}

void RUDPSession::RecvContextReset()
{
	rioContext.RecvContextReset();
}

RIO_RQ RUDPSession::GetRecvRIORQ() const
{
	return rioContext.GetRIORQ();
}

RIO_RQ RUDPSession::GetSendRIORQ() const
{
	return rioContext.GetRIORQ();
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

	if (not stateMachine.TryTransitionToConnected())
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
	DoDisconnect(DISCONNECT_REASON::NORMAL);
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
	NetBuffer* buffer = NetBuffer::Alloc();
	if (buffer == nullptr)
	{
		LOG_ERROR("Buffer is nullptr in RUDPSession::SendReplyToClient()");
		DoDisconnect(DISCONNECT_REASON::BY_ERROR);
		return;
	}

	auto packetType = PACKET_TYPE::SEND_REPLY_TYPE;
	const BYTE advertiseWindow = flowManager.GetAdvertisableWindow();
	*buffer << packetType << recvPacketSequence << advertiseWindow;

	if (not SendPacket(*buffer, recvPacketSequence, true, true))
	{
		DoDisconnect(DISCONNECT_REASON::BY_ERROR);
	}
}

void RUDPSession::OnSendReply(NetBuffer& recvPacket)
{
	PacketSequence packetSequence;
	recvPacket >> packetSequence;

	if (rioContext.GetSendContext().GetLastSendPacketSequence() < packetSequence)
	{
		return;
	}

	SendPacketInfo* sendPacketInfo = rioContext.GetSendContext().FindAndEraseSendPacketInfo(packetSequence);
	if (sendPacketInfo == nullptr)
	{
		LOG_DEBUG(std::format(
			"[TEMP_TPS_TRACE][SERVER_ACK_MISS] sessionId={} seq={}",
			sessionId,
			packetSequence));
		return;
	}

	flowManager.OnAckReceived(packetSequence);
	core.EraseSendPacketInfo(sendPacketInfo, threadId);

	TryFlushPendingQueue();
}

void RUDPSession::OnRetransmissionTimeout() noexcept
{
	LOG_DEBUG(std::format(
		"[TEMP_TPS_TRACE][SERVER_RETRANSMIT_TIMEOUT] sessionId={}",
		sessionId));
	flowManager.OnTimeout();
}

std::shared_mutex& RUDPSession::GetSocketMutex() const
{
	return socketContext.GetSocketMutex();
}

SessionIdType RUDPSession::GetSessionId() const
{
	return sessionId;
}

SOCKET RUDPSession::GetSocket() const
{
	return socketContext.GetSocket();
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
	return stateMachine.IsConnected();
}

bool RUDPSession::IsReserved() const
{
	return stateMachine.IsReserved();
}

bool RUDPSession::IsUsingSession() const
{
	return stateMachine.IsUsingSession();
}

SESSION_STATE RUDPSession::GetSessionState() const
{
	return stateMachine.GetSessionState();
}

bool RUDPSession::IsReleasing() const
{
	return nowInReleaseThread.load(std::memory_order_seq_cst);
}

uint32_t RUDPSession::GetSessionGeneration() const
{
	return sessionGeneration.load(std::memory_order_acquire);
}

bool RUDPSession::CanProcessPacket(const sockaddr_in& targetClientAddr) const
{
	return CheckMyClient(targetClientAddr) && not IsReleasing();
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

void RUDPSession::SetStateMachineToDisconnect()
{
	stateMachine.SetDisconnected();
}

DISCONNECT_REASON RUDPSession::GetDisconnectedReason() const
{
	return disconnectedReason;
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
	return rioContext.GetSendContext();
}

const SessionSendContext& RUDPSession::GetSendContext() const
{
	return rioContext.GetSendContext();
}
