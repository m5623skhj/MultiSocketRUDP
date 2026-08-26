#pragma once
#include <functional>
#include <any>
#include <unordered_map>
#include "NetServerSerializeBuffer.h"

using PacketId = unsigned int;

class RUDPSession;

// ----------------------------------------
// @brief 콘텐츠 패킷의 식별자와 NetBuffer 직렬화 계약을 정의하는 기본 인터페이스입니다.
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
// @brief PacketId별 패킷 생성 함수를 보관하는 정적 팩토리 레지스트리입니다.
// 서버 시작 전에 패킷 타입 등록을 완료해야 하며 런타임 동시 등록은 지원하지 않습니다.
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

public:
	using PacketFactoryFunction = std::function<std::shared_ptr<IPacket>()>;

	// ----------------------------------------
	// @brief IPacket 파생 타입의 기본 생성 팩토리를 해당 PacketId에 등록합니다.
	// 같은 ID를 다시 등록하면 기존 팩토리를 교체합니다.
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

private:
	inline static std::unordered_map<PacketId, PacketFactoryFunction> packetFactoryFunctionMap = {};
};
