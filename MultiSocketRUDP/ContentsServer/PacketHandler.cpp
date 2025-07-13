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
	static bool HandlePacket(const RUDPSession& session, Ping& packet)
	{
		auto pc = GET_PC(pc, session);
		
		Pong pong;
		pc->SendPacket(pong);

		return true;
	}

	static bool HandlePacket(const RUDPSession& session, TestPacketReq& packet)
	{
		auto pc = GET_PC(pc, session);

		TestPacketRes res;
		res.order = packet.order;
		pc->SendPacket(res);

		return true;
	}

	static bool HandlePacket(const RUDPSession& session, TestStringPacketReq& packet)
	{
		auto pc = GET_PC(pc, session);

		TestStringPacketRes res;
		res.echoString = packet.testString;
		pc->SendPacket(res);

		return true;
	}

	void Init()
	{
		PacketHandlerUtil::RegisterPacket<Ping>(HandlePacket);
		PacketHandlerUtil::RegisterPacket<TestStringPacketReq>(HandlePacket);
		PacketHandlerUtil::RegisterPacket<TestPacketReq>(HandlePacket);
	}
}