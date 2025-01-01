#include "PreCompile.h"
#include "PC.h"
#include "RUDPSession.h"

PC::PC(RUDPSession& inSession)
	: session(inSession)
{
}

SessionIdType PC::GetSessionId()
{
	return session.GetSessionId();
}