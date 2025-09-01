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

	[[nodiscard]]
	virtual PacketId GetPacketId() const = 0;

public:
	virtual void BufferToPacket([[maybe_unused]] NetBuffer& buffer) { UNREFERENCED_PARAMETER(buffer); }
	virtual void PacketToBuffer([[maybe_unused]] NetBuffer& buffer) { UNREFERENCED_PARAMETER(buffer); }
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
	static std::shared_ptr<IPacket> MakePacket(PacketId packetId);
	[[nodiscard]]
	static PacketHandler GetPacketHandler(PacketId packetId);
	[[nodiscard]]
	static bool BufferToPacket(PacketId packetId, NetBuffer& buffer, std::any& packet);
	static void Init();

public:
	using PacketFactoryFunction = std::function<std::shared_ptr<IPacket>()>;
	using PacketToBufferFunction = std::function<void (NetBuffer&, std::any&)>;

	template <typename PacketType>
	static void RegisterPacket()
	{
		static_assert(std::is_base_of_v<IPacket, PacketType>, "RegisterPacket() : PacketType must inherit from IPacket");

		PacketFactoryFunction factoryFunc = []()
		{
			return std::make_shared<PacketType>();
		};

		PacketType packetType;
		packetFactoryFunctionMap[packetType.GetPacketId()] = factoryFunc;
	}

	template <typename PacketType>
	static void RegisterPacketHandler(PacketHandler& handler)
	{
		static_assert(std::is_base_of_v<IPacket, PacketType>, "RegisterPacketHandler() : PacketType must inherit from IPacket");

		PacketType packetType;
		packetHandlerMap[packetType.GetPacketId()] = handler;
	}

	template <typename PacketType>
	static void RegisterBufferToPacketType()
	{
		static_assert(std::is_base_of_v<IPacket, PacketType>, "RegisterPacketHandler() : PacketType must inherit from IPacket");

		auto packetToBufferFunction = [](NetBuffer& buffer, std::any& packet)
		{
			auto& realPacket = *static_cast<PacketType*>(std::any_cast<IPacket*>(packet));
			realPacket.BufferToPacket(buffer);
		};

		PacketType packetType;
		packetToBufferFunctionMap[packetType.GetPacketId()] = packetToBufferFunction;
	}

private:
	inline static std::unordered_map<PacketId, PacketFactoryFunction> packetFactoryFunctionMap = {};
	inline static std::unordered_map<PacketId, PacketToBufferFunction> packetToBufferFunctionMap = {};
	inline static std::unordered_map<PacketId, PacketHandler> packetHandlerMap = {};
};