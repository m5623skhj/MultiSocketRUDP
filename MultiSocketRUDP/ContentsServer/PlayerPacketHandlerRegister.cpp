#include "PreCompile.h"
#include "Player.h"
#include "Protocol.h"
#include "PacketHandlerUtil.h"
#include "PlayerPacketHandlerRegister.h"

namespace ContentsPacketRegister
{
	void Init()
	{
		PacketHandlerUtil::RegisterPacket<Ping>();
		PacketHandlerUtil::RegisterPacket<TestStringPacketReq>();
		PacketHandlerUtil::RegisterPacket<TestPacketReq>();
	}
}
