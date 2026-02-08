#pragma once
#include "RIOManager.h"

class RUDPSession;
class RIOManager;
class RUDPSessionManager;

class RUDPSessionFunctionDelegate
{
	friend RIOManager;
	friend RUDPSessionManager;

private:
	RUDPSessionFunctionDelegate() = default;
	~RUDPSessionFunctionDelegate() = default;

private:
#pragma region For RIOManager
	[[nodiscard]]
	static bool InitializeSessionRIO(RUDPSession& session, const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable, const RIO_CQ& recvCQ, const RIO_CQ& sendCQ);
#pragma endregion For RIOManager

#pragma region For SessionManager
	static void SetSessionId(RUDPSession& session, const SessionIdType sessionId);
	static void SetThreadId(RUDPSession& session, const ThreadIdType threadId);
	static void CloseSocket(RUDPSession& session);
	static void RecvContextReset(RUDPSession& session);
	static void SendHeartbeatPacket(RUDPSession& session);
	static bool CheckReservedSessionTimeout(const RUDPSession& session, const unsigned long long now);
	static void AbortReservedSession(RUDPSession& session);
#pragma endregion For SessionManager
};