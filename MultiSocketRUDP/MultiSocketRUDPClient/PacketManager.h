#pragma once
#include <functional>
#include <any>
#include <unordered_map>
#include "NetServerSerializeBuffer.h"

using PacketId = unsigned int;

class RUDPSession;

// ----------------------------------------
// @brief 클라이언트 콘텐츠 패킷의 식별자와 NetBuffer 직렬화 계약을 정의하는 기본 인터페이스입니다.
// ----------------------------------------
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

// ----------------------------------------
// @brief PacketId별 생성·변환·처리 함수를 보관하는 클라이언트 패킷 레지스트리입니다.
// 클라이언트 시작 전에 등록을 완료해야 하며 런타임 동시 등록은 지원하지 않습니다.
// ----------------------------------------
class PacketManager
{
private:
	PacketManager() = default;
	~PacketManager() = default;

	PacketManager(const PacketManager&) = delete;
	PacketManager& operator=(const PacketManager&) = delete;

public:
	// ----------------------------------------
	// @brief 프로세스 내 PacketManager 단일 인스턴스를 반환합니다.
	// ----------------------------------------
	static PacketManager& GetInst();
	// ----------------------------------------
	// @brief 등록된 ID에 대응하는 새 패킷 인스턴스를 생성합니다.
	// @return 등록되지 않은 ID이면 nullptr을 반환합니다.
	// ----------------------------------------
	[[nodiscard]]
	static std::shared_ptr<IPacket> MakePacket(PacketId packetId);
	static void Init();

#pragma region TODO : Packet handler direct call
public:
	using PacketFactory = std::function<std::function<void()>(RUDPSession*, NetBuffer*)>;

	// ----------------------------------------
	// @brief 수신 버퍼를 등록된 패킷 타입으로 역직렬화합니다.
	// @return 등록되지 않은 ID이면 nullptr을 반환합니다.
	// ----------------------------------------
	static std::shared_ptr<IPacket> BufferToPacket(NetBuffer& buffer, const PacketId packetId)
	{
		std::shared_ptr<IPacket> packet = MakePacket(packetId);
		if (packet != nullptr)
		{
			packet->BufferToPacket(buffer);
		}

		return packet;
	}

private:
	std::unordered_map<PacketId, PacketFactory> packetFactoryMap;

#pragma endregion TODO : Packet handler direct call

public:
	using PacketFactoryFunction = std::function<std::shared_ptr<IPacket>()>;
	using PacketToBufferFunction = std::function<void (NetBuffer&, std::any&)>;

	// ----------------------------------------
	// @brief IPacket 파생 타입의 기본 생성 팩토리를 해당 PacketId에 등록합니다.
	// ----------------------------------------
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

	// ----------------------------------------
	// @brief PacketType의 ID에 콘텐츠 패킷 처리 함수를 등록합니다.
	// ----------------------------------------
	template <typename PacketType>
	static void RegisterPacketHandler(PacketHandler& handler)
	{
		static_assert(std::is_base_of_v<IPacket, PacketType>, "RegisterPacketHandler() : PacketType must inherit from IPacket");

		PacketType packetType;
		packetHandlerMap[packetType.GetPacketId()] = handler;
	}

	// ----------------------------------------
	// @brief 수신 NetBuffer를 PacketType으로 변환하는 함수를 해당 ID에 등록합니다.
	// ----------------------------------------
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
