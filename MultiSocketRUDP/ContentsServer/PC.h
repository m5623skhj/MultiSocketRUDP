#pragma once
#include "CoreType.h"

class RUDPSession;
class IPacket;

class PC
{
public:
	PC() = delete;
	~PC() = default;
	explicit PC(RUDPSession& inSession);

public:
	SessionIdType GetSessionId();
	void SendPacket(IPacket& packet);

	bool IsConnected() const;

private:
	RUDPSession& session;
};