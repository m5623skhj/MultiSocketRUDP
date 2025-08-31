#pragma once
#include "RUDPSession.h"

class Player final : public RUDPSession
{
public:
	Player() = delete;
	~Player() override = default;
	explicit Player(MultiSocketRUDPCore& inCore);

public:

private:
	void RegisterAllPacketHandler();
};