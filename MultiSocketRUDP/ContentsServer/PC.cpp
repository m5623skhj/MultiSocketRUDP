#include "PreCompile.h"
#include "PC.h"
#include "RUDPSession.h"
#include "Protocol.h"

PC::PC(RUDPSession& inSession)
	: session(inSession)
{
}

SessionIdType PC::GetSessionId()
{
	return session.GetSessionId();
}

void PC::SendPacket(IPacket& packet)
{
	session.SendPacket(packet);
}