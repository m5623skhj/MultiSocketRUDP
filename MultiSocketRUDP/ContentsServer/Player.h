#pragma once
#include "RUDPSession.h"
#include "Protocol.h"

class Player final : public RUDPSession
{
public:
	Player() = delete;
	~Player() override = default;
	explicit Player(MultiSocketRUDPCore& inCore);

public:
	void OnPing(const Ping& packet);
	void OnTestPacketReq(const TestPacketReq& packet);
	void OnTestStringPacketReq(const TestStringPacketReq& packet);

private:
	void RegisterAllPacketHandler();
};