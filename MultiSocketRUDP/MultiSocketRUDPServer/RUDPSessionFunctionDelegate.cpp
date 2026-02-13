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

std::shared_ptr<IOContext> RUDPSessionFunctionDelegate::GetRecvBufferContext(const RUDPSession& session)
{
	return session.GetRecvBufferContext();
}

RIO_BUFFERID RUDPSessionFunctionDelegate::GetSendBufferId(const RUDPSession& session)
{
	return session.GetSendBufferId();
}

IO_MODE& RUDPSessionFunctionDelegate::GetSendIOMode(RUDPSession& session)
{
	return session.GetSendIOMode();
}

bool RUDPSessionFunctionDelegate::IsSendPacketInfoQueueEmpty(RUDPSession& session)
{
	return session.GetSendPacketInfoQueueSize() == 0;
}

SendPacketInfo* RUDPSessionFunctionDelegate::GetReservedSendPacketInfo(const RUDPSession& session)
{
	return session.GetReservedSendPacketInfo();
}

void RUDPSessionFunctionDelegate::EnqueueToRecvBufferList(RUDPSession& session, NetBuffer* buffer)
{
	session.EnqueueToRecvBufferList(buffer);
}

std::set<MultiSocketRUDP::PacketSequenceSetKey>& RUDPSessionFunctionDelegate::GetPacketSequenceSet(RUDPSession& session)
{
	return session.GetCachedSequenceSet();
}

std::mutex& RUDPSessionFunctionDelegate::GetCachedSequenceSetMutex(RUDPSession& session)
{
	return session.GetCachedSequenceSetMutex();
}

size_t RUDPSessionFunctionDelegate::GetSendPacketInfoQueueSize(const RUDPSession& session)
{
	return session.GetSendPacketInfoQueueSize();
}

char* RUDPSessionFunctionDelegate::GetRIOSendBuffer(RUDPSession& session)
{
	return session.GetRIOSendBuffer();
}

void RUDPSessionFunctionDelegate::SetReservedSendPacketInfo(RUDPSession& session, SendPacketInfo* reserveSendPacketInfo)
{
	return session.SetReservedSendPacketInfo(reserveSendPacketInfo);
}

SendPacketInfo* RUDPSessionFunctionDelegate::GetSendPacketInfoQueueFrontAndPop(RUDPSession& session)
{
	return session.GetSendPacketInfoQueueFrontAndPop();
}

RecvBuffer& RUDPSessionFunctionDelegate::GetRecvBuffer(RUDPSession& session)
{
	return session.GetRecvBuffer();
}

void RUDPSessionFunctionDelegate::SetAbortReservedSession(RUDPSession& session)
{
	session.AbortReservedSession();
}

void RUDPSessionFunctionDelegate::SetSessionReservedTime(RUDPSession& session, const unsigned long long now)
{
	session.sessionReservedTime = now;
}

unsigned char* RUDPSessionFunctionDelegate::GetSessionKeyObjectBuffer(const RUDPSession& session)
{
	return session.keyObjectBuffer;
}

void RUDPSessionFunctionDelegate::SetSessionKeyObjectBuffer(RUDPSession& session, unsigned char* inKeyObjectBuffer)
{
	session.keyObjectBuffer = inKeyObjectBuffer;
}

void RUDPSessionFunctionDelegate::Disconnect(RUDPSession& session, NetBuffer& recvPacket)
{
	session.Disconnect(recvPacket);
}

std::shared_mutex& RUDPSessionFunctionDelegate::GetSocketMutex(const RUDPSession& session)
{
	return session.GetSocketMutex();
}

std::mutex& RUDPSessionFunctionDelegate::AcquireSendPacketInfoQueueLock(RUDPSession& session)
{
	return session.GetSendPacketInfoQueueMutex();
}

SOCKET RUDPSessionFunctionDelegate::GetSocket(const RUDPSession& session)
{
	return session.GetSocket();
}

RIO_RQ RUDPSessionFunctionDelegate::GetRecvRIORQ(const RUDPSession& session)
{
	return session.GetRecvRIORQ();
}

RIO_RQ RUDPSessionFunctionDelegate::GetSendRIORQ(const RUDPSession& session)
{
	return session.GetSendRIORQ();
}

const unsigned char* RUDPSessionFunctionDelegate::GetSessionKey(const RUDPSession& session)
{
	return session.GetSessionKey();
}

void RUDPSessionFunctionDelegate::SetSessionKey(RUDPSession& session, const unsigned char* inSessionKey)
{
	session.SetSessionKey(inSessionKey);
}

const unsigned char* RUDPSessionFunctionDelegate::GetSessionSalt(const RUDPSession& session)
{
	return session.GetSessionSalt();
}

void RUDPSessionFunctionDelegate::SetSessionSalt(RUDPSession& session, const unsigned char* inSessionSalt)
{
	session.SetSessionSalt(inSessionSalt);
}

const BCRYPT_KEY_HANDLE& RUDPSessionFunctionDelegate::GetSessionKeyHandle(const RUDPSession& session)
{
	return session.GetSessionKeyHandle();
}

void RUDPSessionFunctionDelegate::SetSessionKeyHandle(RUDPSession& session, const BCRYPT_KEY_HANDLE& inKeyHandle)
{
	session.SetSessionKeyHandle(inKeyHandle);
}

void RUDPSessionFunctionDelegate::GetServerPortAndSessionId(const RUDPSession& session, PortType& outServerPort, SessionIdType& outSessionId)
{
	outServerPort = session.serverPort;
	outSessionId = session.sessionId;
}
