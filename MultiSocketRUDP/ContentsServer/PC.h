#pragma once
#include "CoreType.h"

class RUDPSession;

class PC
{
public:
	PC() = delete;
	~PC() = default;
	explicit PC(RUDPSession& inSession);

public:
	SessionIdType GetSessionId();

private:
	RUDPSession& session;
};