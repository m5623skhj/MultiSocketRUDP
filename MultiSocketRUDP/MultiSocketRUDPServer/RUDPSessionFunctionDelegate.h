#pragma once
#include "RIOManager.h"

class RUDPSession;

class RUDPSessionFunctionDelegate
{
	friend RIOManager;

private:
	RUDPSessionFunctionDelegate() = default;
	~RUDPSessionFunctionDelegate() = default;

private:
	static bool InitializeSessionRIO(RUDPSession& session, const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable, const RIO_CQ& recvCQ, const RIO_CQ& sendCQ);
};