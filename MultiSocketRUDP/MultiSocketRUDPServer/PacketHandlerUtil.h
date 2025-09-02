#pragma once
#include "PacketManager.h"

namespace PacketHandlerUtil
{
	template <typename PacketType>
	void RegisterPacket()
	{
		REGISTER_PACKET(PacketType)
	}
}