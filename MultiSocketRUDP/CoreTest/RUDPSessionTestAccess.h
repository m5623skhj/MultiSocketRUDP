#pragma once

#include "RUDPSession.h"

class RUDPSessionBehaviorAccess
{
public:
	static bool OnRecvPacket(RUDPSession& session, NetBuffer& recvPacket)
	{
		return session.OnRecvPacket(recvPacket);
	}

	static bool IsOlderRecvSequence(
		const PacketSequence sequence,
		const PacketSequence expectedSequence)
	{
		return RUDPSession::IsOlderRecvSequence(sequence, expectedSequence);
	}

	static void OnSendReply(RUDPSession& session, NetBuffer& recvPacket)
	{
		session.OnSendReply(recvPacket);
	}

	static SessionSendContext& GetSendContext(RUDPSession& session)
	{
		return session.GetSendContext();
	}

	static void InitializeSession(RUDPSession& session)
	{
		session.InitializeSession();
	}

	static RecvBuffer& GetRecvBuffer(RUDPSession& session)
	{
		return session.GetRecvBuffer();
	}

	static bool CanFinalizeIO(RUDPSession& session)
	{
		return session.CanFinalizeIO();
	}

	static void BeginIOShutdown(RUDPSession& session)
	{
		session.BeginIOShutdown();
	}

	static void BeginIOCompletion(RUDPSession& session)
	{
		session.BeginIOCompletion();
	}

	static void CompleteIOCompletion(RUDPSession& session)
	{
		session.CompleteIOCompletion();
	}

	static void RefreshLastReceivedPacketTime(
		RUDPSession& session,
		const unsigned long long now)
	{
		session.RefreshLastReceivedPacketTime(now);
	}

	static void SetReserved(RUDPSession& session)
	{
		session.stateMachine.SetReserved();
	}

	static void SetConnected(RUDPSession& session)
	{
		session.stateMachine.SetReserved();
		std::ignore = session.stateMachine.TryTransitionToConnected();
	}

	static void SetReleasing(RUDPSession& session)
	{
		session.stateMachine.SetReserved();
		std::ignore = session.stateMachine.TryTransitionToReleasing();
	}

	static void SetSessionId(RUDPSession& session, const SessionIdType sessionId)
	{
		session.SetSessionId(sessionId);
	}

	static void SetSessionReservedTime(RUDPSession& session, const unsigned long long reservedTime)
	{
		session.sessionReservedTime = reservedTime;
	}

	static void SetClientAddress(RUDPSession& session, const sockaddr_in& clientAddress)
	{
		session.clientAddr = clientAddress;
		session.clientSockAddrInet = {};
		session.clientSockAddrInet.Ipv4 = clientAddress;
	}

	static void SetNowInReleaseThread(RUDPSession& session, const bool isReleasing)
	{
		session.nowInReleaseThread.store(isReleasing, std::memory_order_release);
	}

	static void SetDisconnectedReason(RUDPSession& session, const DISCONNECT_REASON reason)
	{
		session.disconnectedReason = reason;
	}

	static bool NeedToSendHeartbeat(const RUDPSession& session, const unsigned long long now)
	{
		return session.NeedToSendHeartbeat(now);
	}

	static bool CheckReservedSessionTimeout(const RUDPSession& session, const unsigned long long now)
	{
		return session.CheckReservedSessionTimeout(now);
	}

	static void SetReservedSessionTimeoutMs(const unsigned long long timeoutMs)
	{
		RUDPSession::reservedSessionTimeoutMs = timeoutMs;
	}

	static bool CanProcessPacket(const RUDPSession& session, const sockaddr_in& clientAddress)
	{
		return session.CanProcessPacket(clientAddress);
	}

	static bool TryConnect(RUDPSession& session, NetBuffer& recvPacket, const sockaddr_in& clientAddress)
	{
		return session.TryConnect(recvPacket, clientAddress);
	}

	static void SetMaximumPacketHoldingQueueSize(const BYTE size)
	{
		RUDPSession::SetMaximumPacketHoldingQueueSize(size);
	}
};
