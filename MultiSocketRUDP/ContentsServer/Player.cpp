#include "PreCompile.h"
#include "Player.h"

Player::Player(MultiSocketRUDPCore& inCore)
	: RUDPSession(inCore)
{
	RegisterAllPacketHandler();
}