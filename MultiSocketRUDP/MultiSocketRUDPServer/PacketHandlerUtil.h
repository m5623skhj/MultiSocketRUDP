#pragma once
#include "PacketManager.h"

namespace PacketHandlerUtil
{
	template <typename PacketType>
	PacketHandler MakePacketHandler(PacketHandler targetFunction)
	{
		return [targetFunction](RUDPSession& session, IPacket& inPacket)
		{
			static_assert(std::is_base_of<IPacket, PacketType>::value, "MakePacketHandler() : PacketType must inherit from IPacket");

			auto* packet = static_cast<PacketType*>(&inPacket);
			return targetFunction(session, *packet);
		};
	}

	template <typename PacketType>
	PacketHandler WappingHandler(bool (*targetFunction)(RUDPSession&, PacketType&))
	{
		return [targetFunction](RUDPSession& session, IPacket& inPacket) -> bool
		{
			auto* packet = static_cast<PacketType*>(&inPacket);
			return targetFunction(session, *packet);
		};
	}

	template <typename PacketType>
	void RegisterPacket(bool (*targetFunction)(RUDPSession&, PacketType&))
	{
		PacketHandler handler = PacketHandlerUtil::MakePacketHandler<PacketType>(PacketHandlerUtil::WappingHandler(targetFunction));
		REGISTER_PACKET(PacketType, handler);
	}
}