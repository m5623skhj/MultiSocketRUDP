#pragma once

#include <functional>
#include <any>
#include <unordered_map>
#include "NetServerSerializeBuffer.h"

using PacketId = unsigned int;

class RUDPSession;

class IPacket
{
public:
	IPacket() = default;
	virtual ~IPacket() = default;

	virtual PacketId GetPacketId() const = 0;

public:
	virtual void BufferToPacket([[maybe_unused]] NetBuffer& buffer) { buffer; }
	virtual void PacketToBuffer([[maybe_unused]] NetBuffer& buffer) { buffer; }
};
using PacketHandler = std::function<bool(RUDPSession&, IPacket&)>;

class PacketManager
{
private:
	PacketManager() = default;
	~PacketManager() = default;

	PacketManager(const PacketManager&) = delete;
	PacketManager& operator=(const PacketManager&) = delete;

public:
	static PacketManager& GetInst();
	[[nodiscard]]
	std::shared_ptr<IPacket> MakePacket(PacketId packetId);
	[[nodiscard]]
	PacketHandler GetPacketHandler(PacketId packetId);
	[[nodiscard]]
	bool BufferToPacket(PacketId packetId, NetBuffer& buffer, std::any& packet);
	void Init();

public:
	using PacketFactoryFunction = std::function<std::shared_ptr<IPacket>()>;
	using PacketToBufferFunction = std::function<void (NetBuffer&, std::any&)>;

	template <typename PacketType>
	void RegisterPacket()
	{
		static_assert(std::is_base_of<IPacket, PacketType>::value, "RegisterPacket() : PacketType must inherit from IPacket");
		PacketFactoryFunction factoryFunc = []()
		{
			return std::make_shared<PacketType>();
		};

		PacketType packetType;
		packetFactoryFunctionMap[packetType.GetPacketId()] = factoryFunc;
	}

	//template <typename PacketType>
	//void RegisterPacketHandler()
	//{
	//	static_assert(std::is_base_of<IPacket, PacketType>::value, "RegisterPacketHandler() : PacketType must inherit from IPacket");
	//	auto handler = [](RUDPSession& session, NetBuffer& buffer, std::any& packet)
	//	{
	//		auto realPacket = static_cast<PacketType*>(std::any_cast<IPacket*>(packet));
	//		realPacket->BufferToPacket(buffer);
	//		return HandlePacket(session, *realPacket);
	//	};

	//	PacketType packetType;
	//	packetHandlerMap[packetType.GetPacketId()] = handler;
	//}

	template <typename PacketType>
	void BufferToPacketType(NetBuffer& buffer, std::any& packet)
	{
		static_assert(std::is_base_of<IPacket, PacketType>::value, "RegisterPacketHandler() : PacketType must inherit from IPacket");
		void packetToBufferFunction = [](NetBuffer& buffer, std::any& packet)
		{
			auto realPacket = static_cast<PacketType*>(std::any_cast<IPacket*>(packet));
			realPacket->BufferToPacket(buffer);
		};

		PacketType packetType;
		packetToBufferFunctionMap[packetType.GetPacketId()] = packetToBufferFunction;
	}

	template <typename PacketType>
	void RegisterPacketHandler(PacketHandler& handler)
	{
		static_assert(std::is_base_of<IPacket, PacketType>::value, "RegisterPacketHandler() : PacketType must inherit from IPacket");

		PacketType packetType;
		packetHandlerMap[packetType.GetPacketId()] = handler;
	}

private:
	std::unordered_map<PacketId, PacketFactoryFunction> packetFactoryFunctionMap;
	std::unordered_map<PacketId, PacketToBufferFunction> packetToBufferFunctionMap;
	std::unordered_map<PacketId, PacketHandler> packetHandlerMap;
};