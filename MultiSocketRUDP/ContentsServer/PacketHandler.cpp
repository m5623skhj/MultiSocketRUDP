#include "PreCompile.h"
#include "PacketHandler.h"
#include "../MultiSocketRUDPServer/PacketHandlerUtil.h"

namespace ContentsPacketHandler
{
	bool HandlePacket(RUDPSession& session, Ping& packet)
	{
		return true;
	}

	void Init()
	{
		PacketHandlerUtil::RegisterPacket<Ping>(HandlePacket);
	}
}