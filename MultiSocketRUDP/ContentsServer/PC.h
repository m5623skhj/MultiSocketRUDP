#pragma once
#include "CoreType.h"
#include "RUDPSession.h"

class IPacket;

class PC
{
public:
	PC() = delete;
	~PC() = default;
	explicit PC(RUDPSession& inSession);

public:
	[[nodiscard]]
	SessionIdType GetSessionId() const;
	void SendPacket(IPacket& packet) const;

	[[nodiscard]]
	bool IsConnected() const;

private:
	RUDPSession& session;
};
