#include "PreCompile.h"
#include "PacketIdType.h"
#include "Player.h"

void Player::RegisterAllPacketHandler()
{
	RegisterPacketHandler<Player, Ping>(static_cast<PacketId>(PACKET_ID::PING), &Player::OnPing);
	RegisterPacketHandler<Player, TestPacketReq>(static_cast<PacketId>(PACKET_ID::TEST_PACKET_REQ), &Player::OnTestPacketReq);
	RegisterPacketHandler<Player, TestStringPacketReq>(static_cast<PacketId>(PACKET_ID::TEST_STRING_PACKET_REQ), &Player::OnTestStringPacketReq);
}