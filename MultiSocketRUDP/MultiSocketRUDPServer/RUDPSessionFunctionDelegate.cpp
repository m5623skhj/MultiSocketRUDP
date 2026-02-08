#include "PreCompile.h"
#include "RUDPSessionFunctionDelegate.h"
#include "RUDPSession.h"
#include "RIOManager.h"
#include "RUDPSessionManager.h"

bool RUDPSessionFunctionDelegate::InitializeSessionRIO(RUDPSession& session, const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable, const RIO_CQ& recvCQ, const RIO_CQ& sendCQ)
{
	return session.InitializeRIO(rioFunctionTable, recvCQ, sendCQ);
}

void RUDPSessionFunctionDelegate::SetSessionId(RUDPSession& session, const SessionIdType sessionId)
{
	session.SetSessionId(sessionId);
}

void RUDPSessionFunctionDelegate::SetThreadId(RUDPSession& session, const ThreadIdType threadId)
{
	session.SetThreadId(threadId);
}

void RUDPSessionFunctionDelegate::CloseSocket(RUDPSession& session)
{
	session.CloseSocket();
}

void RUDPSessionFunctionDelegate::RecvContextReset(RUDPSession& session)
{
	session.RecvContextReset();
}

void RUDPSessionFunctionDelegate::SendHeartbeatPacket(RUDPSession& session)
{
	session.SendHeartbeatPacket();
}

bool RUDPSessionFunctionDelegate::CheckReservedSessionTimeout(const RUDPSession& session, const unsigned long long now)
{
	return session.CheckReservedSessionTimeout(now);
}

void RUDPSessionFunctionDelegate::AbortReservedSession(RUDPSession& session)
{
	session.AbortReservedSession();
}