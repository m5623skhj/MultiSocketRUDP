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
	static void Init();

public:
	using PacketFactoryFunction = std::function<std::shared_ptr<IPacket>()>;

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

private:
	inline static std::unordered_map<PacketId, PacketFactoryFunction> packetFactoryFunctionMap = {};
};