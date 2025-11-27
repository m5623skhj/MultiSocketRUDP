#include "PreCompile.h"
#include "Player.h"

#include "LogExtension.h"
#include "Logger.h"

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
	LOG_DEBUG(std::format("Player connected. Current player count {}", nowPlayerCount));
}

void Player::OnDisconnected()
{
	const auto nowPlayerCount = Local::playerCount.fetch_sub(1, std::memory_order_relaxed) - 1;
	LOG_DEBUG(std::format("Player disconnected. Current player count {}", nowPlayerCount));
}