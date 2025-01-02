#include "PreCompile.h"
#include "PacketHandler.h"
#include "../MultiSocketRUDPServer/PacketHandlerUtil.h"
#include "PCManager.h"
#include "RUDPSession.h"
#include "PC.h"

#define GET_PC(pc, session) \
	PCManager::GetInst().FindPC(session.GetSessionId()); \
	if (pc == nullptr) \
	{ \
		return false; \
	}

namespace ContentsPacketHandler
{
	bool HandlePacket(RUDPSession& session, Ping& packet)
	{
		auto pc = GET_PC(pc, session);

		return true;
	}

	bool HandlePacket(RUDPSession& session, TestStringPacket& packet)
	{
		auto pc = GET_PC(pc, session);

		return true;
	}

	bool HandlePacket(RUDPSession& session, TestPacketReq& packet)
	{
		auto pc = GET_PC(pc, session);

		return true;
	}

	void Init()
	{
		PacketHandlerUtil::RegisterPacket<Ping>(HandlePacket);
		PacketHandlerUtil::RegisterPacket<TestStringPacket>(HandlePacket);
		PacketHandlerUtil::RegisterPacket<TestPacketReq>(HandlePacket);
	}
}