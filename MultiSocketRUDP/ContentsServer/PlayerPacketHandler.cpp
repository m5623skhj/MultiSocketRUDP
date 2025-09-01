#include "PreCompile.h"
#include "Player.h"

void Player::OnPing(const Ping& packet)
{
	Pong pong;
	SendPacket(pong);
}

void Player::OnTestPacketReq(const TestPacketReq& packet)
{
	TestPacketRes res;
	res.order = packet.order;
	SendPacket(res);
}

void Player::OnTestStringPacketReq(const TestStringPacketReq& packet)
{
	TestStringPacketRes res;
	res.echoString = packet.testString;
	SendPacket(res);
}