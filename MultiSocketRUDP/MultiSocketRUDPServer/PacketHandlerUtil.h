#pragma once
#include "PacketManager.h"

namespace PacketHandlerUtil
{
	template <typename PacketType>
	PacketHandler MakePacketHandler(const PacketHandler& targetFunction)
	{
		return [targetFunction](RUDPSession& session, IPacket& inPacket)
		{
			static_assert(std::is_base_of_v<IPacket, PacketType>, "MakePacketHandler() : PacketType must inherit from IPacket");

			auto* packet = static_cast<PacketType*>(&inPacket);
			return targetFunction(session, *packet);
		};
	}

	template <typename PacketType>
	PacketHandler MappingHandler(bool (*targetFunction)(RUDPSession&, PacketType&))
	{
		return [targetFunction](RUDPSession& session, IPacket& inPacket) -> bool
		{
			auto* packet = static_cast<PacketType*>(&inPacket);
			return targetFunction(session, *packet);
		};
	}

	template <typename PacketType>
	void RegisterPacket(bool (*targetFunction)(const RUDPSession&, PacketType&))
	{
		PacketHandler handler = PacketHandlerUtil::MakePacketHandler<PacketType>(PacketHandlerUtil::MappingHandler(targetFunction));
		REGISTER_PACKET(PacketType, handler);
	}
}