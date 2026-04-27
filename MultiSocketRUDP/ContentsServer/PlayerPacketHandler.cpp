#include "PreCompile.h"
#include "Player.h"

namespace Local
{
	extern std::atomic_uint64_t pingRecvCount; // TEMP_TPS_TRACE
	extern std::atomic_uint64_t pongSendCount; // TEMP_TPS_TRACE
}

#pragma region Packet Handler
void Player::OnPing(const Ping& packet)
{
	Local::pingRecvCount.fetch_add(1, std::memory_order_relaxed); // TEMP_TPS_TRACE
	Pong pong;
	SendPacket(pong);
	Local::pongSendCount.fetch_add(1, std::memory_order_relaxed); // TEMP_TPS_TRACE
}

void Player::OnTestStringPacketReq(const TestStringPacketReq& packet)
{
	TestStringPacketRes res;
	res.echoString = packet.testString;
	SendPacket(res);
}

void Player::OnTestPacketReq(const TestPacketReq& packet)
{
	TestPacketRes res;
	res.order = packet.order;
	SendPacket(res);
}

#pragma endregion Packet Handler
