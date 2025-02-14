#include "PreCompile.h"
#include "PacketManager.h"
#include "RUDPSession.h"

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
	auto iter = packetFactoryFunctionMap.find(packetId);
	if (iter == packetFactoryFunctionMap.end())
	{
		return nullptr;
	}

	auto factoryFunc = iter->second;
	return factoryFunc();
}

PacketHandler PacketManager::GetPacketHandler(const PacketId packetId)
{
	auto iter = packetHandlerMap.find(packetId);
	if (iter == packetHandlerMap.end())
	{
		return nullptr;
	}

	return iter->second;
}

bool PacketManager::BufferToPacket(const PacketId packetId, NetBuffer& buffer, std::any& packet)
{
	auto iter = packetToBufferFunctionMap.find(packetId);
	if (iter == packetToBufferFunctionMap.end())
	{
		return false;
	}

	iter->second(buffer, packet);
	return true;
}