#pragma once
#include "CoreType.h"

class MultiSocketRUDPCore;

class RUDPSession
{
	friend MultiSocketRUDPCore;

private:
	RUDPSession() = delete;
	explicit RUDPSession(SessionIdType inSessionId, SOCKET inSock, PortType inPort);

private:
	static std::shared_ptr<RUDPSession> Create(SessionIdType inSessionId, SOCKET inSock, PortType inPort)
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

public:
	virtual ~RUDPSession();

protected:
	virtual void OnRecv();

private:
	SessionIdType sessionId;
	PortType port;
	SOCKET sock;
	bool isUsingSession{};
};
