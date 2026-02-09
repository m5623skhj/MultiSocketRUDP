#include "PreCompile.h"
#include "RUDPSessionFunctionDelegate.h"
#include "RUDPSession.h"
#include "RIOManager.h"

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

const unsigned char* RUDPSessionFunctionDelegate::GetSessionSalt(const RUDPSession& session)
{
	return session.GetSessionSalt();
}

const BCRYPT_KEY_HANDLE& RUDPSessionFunctionDelegate::GetSessionKeyHandle(const RUDPSession& session)
{
	return session.GetSessionKeyHandle();
}

bool RUDPSessionFunctionDelegate::TryConnect(RUDPSession& session, NetBuffer& recvPacket, const sockaddr_in& clientAddr)
{
	return session.TryConnect(recvPacket, clientAddr);
}

bool RUDPSessionFunctionDelegate::CanProcessPacket(const RUDPSession& session, const sockaddr_in& clientAddr)
{
	return session.CanProcessPacket(clientAddr);
}

void RUDPSessionFunctionDelegate::OnSendReply(RUDPSession& session, NetBuffer& recvPacket)
{
	session.OnSendReply(recvPacket);
}

bool RUDPSessionFunctionDelegate::OnRecvPacket(RUDPSession& session, NetBuffer& recvPacket)
{
	return session.OnRecvPacket(recvPacket);
}

void RUDPSessionFunctionDelegate::Disconnect(RUDPSession& session, NetBuffer& recvPacket)
{
	session.Disconnect(recvPacket);
}
