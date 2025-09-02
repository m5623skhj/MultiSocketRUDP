#include "PreCompile.h"
#include "PacketManager.h"

PacketManager& PacketManager::GetInst()
{
	static PacketManager instance;
	return instance;
}

void PacketManager::Init()
{
}

std::shared_ptr<IPacket> PacketManager::MakePacket(const PacketId packetId)
{
	const auto itor = packetFactoryFunctionMap.find(packetId);
	if (itor == packetFactoryFunctionMap.end())
	{
		return nullptr;
	}

	return itor->second();
}