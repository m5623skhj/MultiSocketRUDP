#pragma once
#include "RUDPSession.h"
#include "Protocol.h"

using PlayerIdType = unsigned long long;
constexpr PlayerIdType InvalidPlayerId = 0;

class Player final : public RUDPSession
{
public:
	Player() = delete;
	~Player() override = default;
	explicit Player(MultiSocketRUDPCore& inCore);

private:
	void OnConnected() override;
	void OnDisconnected() override;
	void OnReleased() override;

private:
	void RegisterAllPacketHandler();

public:
	void SetPlayerId(const PlayerIdType inPlayerId) { playerId = inPlayerId; }
	PlayerIdType GetPlayerId() const { return playerId; }

private:
	PlayerIdType playerId = InvalidPlayerId;

#pragma region Packet Handler
public:
	void OnPing(const Ping& packet);
	void OnTestStringPacketReq(const TestStringPacketReq& packet);
	void OnTestPacketReq(const TestPacketReq& packet);
#pragma endregion Packet Handler
};
