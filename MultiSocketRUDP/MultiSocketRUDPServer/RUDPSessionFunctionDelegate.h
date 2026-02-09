#pragma once
#include "RIOManager.h"
#include "NetServerSerializeBuffer.h"

class RUDPSession;
class RIOManager;
class RUDPSessionManager;
class RUDPPacketProcessor;

class RUDPSessionFunctionDelegate
{
	friend RIOManager;
	friend RUDPSessionManager;
	friend RUDPPacketProcessor;

private:
	RUDPSessionFunctionDelegate() = default;
	~RUDPSessionFunctionDelegate() = default;

private:
#pragma region For RIOManager
	[[nodiscard]]
	static bool InitializeSessionRIO(RUDPSession& session, const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable, const RIO_CQ& recvCQ, const RIO_CQ& sendCQ);
#pragma endregion For RIOManager

#pragma region For SessionManager
	static void SetSessionId(RUDPSession& session, SessionIdType sessionId);
	static void SetThreadId(RUDPSession& session, ThreadIdType threadId);
	static void CloseSocket(RUDPSession& session);
	static void RecvContextReset(RUDPSession& session);
	static void SendHeartbeatPacket(RUDPSession& session);
	static bool CheckReservedSessionTimeout(const RUDPSession& session, unsigned long long now);
	static void AbortReservedSession(RUDPSession& session);
#pragma endregion For SessionManager

#pragma region For RUDPPacketProcessor
	static const unsigned char* GetSessionSalt(const RUDPSession& session);
	static const BCRYPT_KEY_HANDLE& GetSessionKeyHandle(const RUDPSession& session);
	static bool TryConnect(RUDPSession& session, NetBuffer& recvPacket, const sockaddr_in& clientAddr);
	static bool CanProcessPacket(const RUDPSession& session, const sockaddr_in& clientAddr);
	static void OnSendReply(RUDPSession& session, NetBuffer& recvPacket);
	static bool OnRecvPacket(RUDPSession& session, NetBuffer& recvPacket);
#pragma endregion For RUDPPacketProcessor

#pragma region Util
	static void Disconnect(RUDPSession& session, NetBuffer& recvPacket);
#pragma endregion Util
};