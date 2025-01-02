#include "PreCompile.h"
#include "PacketHandler.h"
#include "../MultiSocketRUDPServer/PacketHandlerUtil.h"
#include "PCManager.h"
#include "RUDPSession.h"
#include "PC.h"

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

	bool HandlePacket(RUDPSession& session, TestPacketReq& packet)
	{
		return true;
	}

	void Init()
	{
		PacketHandlerUtil::RegisterPacket<Ping>(HandlePacket);
		PacketHandlerUtil::RegisterPacket<TestStringPacket>(HandlePacket);
		PacketHandlerUtil::RegisterPacket<TestPacketReq>(HandlePacket);
	}
}