#include "PreCompile.h"
#include "PacketHandler.h"
#include "../MultiSocketRUDPServer/PacketHandlerUtil.h"

namespace ContentsPacketHandler
{
	bool HandlePacket(RUDPSession& session, Ping& packet)
	{
		return true;
	}

	template <typename PacketType>
	void RegisterPacket(bool (*targetFunction)(RUDPSession&, PacketType&))
	{
		PacketHandler handler = PacketHandlerUtil::MakePacketHandler<PacketType>(PacketHandlerUtil::WappingHandler(targetFunction));
		REGISTER_PACKET(PacketType, handler);
	}

	void Init()
	{
		RegisterPacket<Ping>(HandlePacket);
	}
}