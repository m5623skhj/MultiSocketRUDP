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
	REGISTER_PACKET_LIST();
}

std::shared_ptr<IPacket> PacketManager::MakePacket(PacketId packetId)
{
	auto iter = packetFactoryFunctionMap.find(packetId);
	if (iter == packetFactoryFunctionMap.end())
	{
		return nullptr;
	}

	auto factoryFunc = iter->second;
	return factoryFunc();
}

PacketHandler PacketManager::GetPacketHandler(PacketId packetId)
{
	auto iter = packetHandlerMap.find(packetId);
	if (iter == packetHandlerMap.end())
	{
		return nullptr;
	}

	return iter->second;
}