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

bool PC::IsConnected() const
{
	return session.IsConnected();
}

void PC::SendPacket(IPacket& packet)
{
	session.SendPacket(packet);
}