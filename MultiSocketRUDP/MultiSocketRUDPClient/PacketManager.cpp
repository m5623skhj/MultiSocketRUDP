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

PacketHandler PacketManager::GetPacketHandler(const PacketId packetId)
{
	const auto itor = packetHandlerMap.find(packetId);
	if (itor == packetHandlerMap.end())
	{
		return nullptr;
	}

	return itor->second;
}

bool PacketManager::BufferToPacket(const PacketId packetId, NetBuffer& buffer, std::any& packet)
{
	const auto itor = packetToBufferFunctionMap.find(packetId);
	if (itor == packetToBufferFunctionMap.end())
	{
		return false;
	}

	itor->second(buffer, packet);
	return true;
}