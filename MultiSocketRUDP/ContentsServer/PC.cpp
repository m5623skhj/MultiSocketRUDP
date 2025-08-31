#include "PreCompile.h"
#include "PC.h"
#include "RUDPSession.h"

PC::PC(RUDPSession& inSession)
	: session(inSession)
{
}

SessionIdType PC::GetSessionId() const
{
	return session.GetSessionId();
}

bool PC::IsConnected() const
{
	return session.IsConnected();
}

void PC::SendPacket(IPacket& packet) const
{
	session.SendPacket(packet);
}