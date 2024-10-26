#include "PreCompile.h"
#include "RUDPSession.h"
#include <WinSock2.h>

RUDPSession::RUDPSession(SessionIdType inSessionId, SOCKET inSock, PortType inPort)
	: sessionId(inSessionId)
	, sock(inSock)
	, port(inPort)
{
}

std::shared_ptr<RUDPSession> RUDPSession::Create(SessionIdType inSessionId, SOCKET inSock, PortType inPort)
{
	struct RUDPSessionCreator : public RUDPSession
	{
		RUDPSessionCreator(SessionIdType inSessionId, SOCKET inSock, PortType inPort)
			: RUDPSession(inSessionId, inSock, inPort)
		{
		}
	};

	return std::make_shared<RUDPSessionCreator>(inSessionId, inSock, inPort);
}

RUDPSession::~RUDPSession()
{
	closesocket(sock);
}

void RUDPSession::OnRecv()
{
}
