#include "PreCompile.h"
#include "Player.h"

#pragma region Packet Handler
void Player::OnPing(const Ping& packet)
{
	Pong pong;
	SendPacket(pong);
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
