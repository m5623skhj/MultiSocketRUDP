#include "PreCompile.h"
#include "RUDPSession.h"
#include <WinSock2.h>

RUDPSession::RUDPSession(SessionIdType inSessionId, SOCKET inSock, PortType inPort)
	: sessionId(inSessionId)
	, sock(inSock)
	, port(inPort)
{
}

RUDPSession::~RUDPSession()
{
	closesocket(sock);
}

void RUDPSession::OnRecv()
{
}
