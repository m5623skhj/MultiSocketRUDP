#pragma once

#include "RUDPSession.h"

class RUDPSessionBehaviorAccess
{
public:
	static bool OnRecvPacket(RUDPSession& session, NetBuffer& recvPacket)
	{
		return session.OnRecvPacket(recvPacket);
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

	static void SetMaximumPacketHoldingQueueSize(const BYTE size)
	{
		RUDPSession::SetMaximumPacketHoldingQueueSize(size);
	}
};
