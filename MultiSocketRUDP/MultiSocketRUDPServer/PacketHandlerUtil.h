#pragma once
#include "PacketManager.h"

namespace PacketHandlerUtil
{
	// ----------------------------------------
	// @brief 콘텐츠 패킷 타입을 PacketManager 팩토리에 등록하는 편의 함수입니다.
	// PacketType은 IPacket을 상속하고 고유한 PacketId를 제공해야 합니다.
	// ----------------------------------------
	template <typename PacketType>
	void RegisterPacket()
	{
		REGISTER_PACKET(PacketType)
	}
}
