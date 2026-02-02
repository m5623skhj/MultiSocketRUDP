#include "PreCompile.h"
#include "RUDPSessionFunctionDelegate.h"
#include "RUDPSession.h"

bool RUDPSessionFunctionDelegate::InitializeSessionRIO(RUDPSession& session, const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable, const RIO_CQ& recvCQ, const RIO_CQ& sendCQ)
{
	return session.InitializeRIO(RUDPSession::RUDPSessionFuncToken(), rioFunctionTable, recvCQ, sendCQ);
}
