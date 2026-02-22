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
	: flowManager(maximumHoldingPacketQueueSize)
	, sessionPacketOrderer(maximumHoldingPacketQueueSize)
	, rioContext()
	, core(inCore)
{
}

bool RUDPSession::InitializeRIO(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable, const RIO_CQ& rioRecvCQ, const RIO_CQ& rioSendCQ)
{
	return rioContext.Initialize(rioFunctionTable, rioRecvCQ, rioSendCQ, socketContext.GetSocket(), sessionId, this);
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
	rioContext.GetSendContext().Reset();
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
		rioContext.GetSendContext().ForEachAndClearSendPacketInfoMap([this](SendPacketInfo* info)
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

	if (const PacketSequence nextSequence = rioContext.GetSendContext().GetLastSendPacketSequence() + 1; not flowManager.CanSend(nextSequence))
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
	const PacketSequence packetSequence = rioContext.GetSendContext().IncrementLastSendPacketSequence();
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
		SendPacketInfo::Free(sendPacketInfo);
		if (not isReplyType)
		{
			rioContext.GetSendContext().EraseSendPacketInfo(inSendPacketSequence);
		}

		return false;
	}

	SendPacketInfo::Free(sendPacketInfo);
	return true;
}

void RUDPSession::SendHeartbeatPacket()
{
	if (const PacketSequence nextSequence = rioContext.GetSendContext().GetLastSendPacketSequence() + 1; not flowManager.CanSend(nextSequence))
	{
		return;
	}

	NetBuffer& buffer = *NetBuffer::Alloc();

	auto packetType = PACKET_TYPE::HEARTBEAT_TYPE;
	const PacketSequence packetSequence = rioContext.GetSendContext().IncrementLastSendPacketSequence();
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

	if (rioContext.GetSendContext().GetLastSendPacketSequence() < packetSequence)
	{
		return;
	}

	SendPacketInfo* sendPacketInfo = rioContext.GetSendContext().FindAndEraseSendPacketInfo(packetSequence);
	if (sendPacketInfo == nullptr)
	{
		return;
	}

	flowManager.OnAckReceived(packetSequence);
	core.EraseSendPacketInfo(sendPacketInfo, threadId);
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
	return rioContext.GetSendContext();
}

const SessionSendContext& RUDPSession::GetSendContext() const
{
	return rioContext.GetSendContext();
}