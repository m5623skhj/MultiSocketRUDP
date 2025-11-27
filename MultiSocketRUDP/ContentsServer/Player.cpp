#include "PreCompile.h"
#include "Player.h"

namespace Local
{
	static std::atomic_int16_t playerCount = 0;
}

Player::Player(MultiSocketRUDPCore& inCore)
	: RUDPSession(inCore)
{
	RegisterAllPacketHandler();
}

void Player::OnConnected()
{
	const auto nowPlayerCount = Local::playerCount.fetch_add(1, std::memory_order_relaxed) + 1;
	std::cout << "Player connected. Current player count: " << nowPlayerCount << '\n';
}

void Player::OnDisconnected()
{
	const auto nowPlayerCount = Local::playerCount.fetch_sub(1, std::memory_order_relaxed) - 1;
	std::cout << "Player disconnected. Current player count: " << nowPlayerCount << '\n';
}