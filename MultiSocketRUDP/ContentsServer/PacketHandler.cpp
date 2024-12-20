#include "PreCompile.h"
#include "PacketHandler.h"
#include "../MultiSocketRUDPServer/PacketHandlerUtil.h"

namespace ContentsPacketHandler
{
	bool HandlePacket(RUDPSession& session, Ping& packet)
	{
		return true;
	}

	bool HandlePacket(RUDPSession& session, TestStringPacket& packet)
	{
		return true;
	}

	void Init()
	{
		PacketHandlerUtil::RegisterPacket<Ping>(HandlePacket);
		PacketHandlerUtil::RegisterPacket<TestStringPacket>(HandlePacket);
	}
}