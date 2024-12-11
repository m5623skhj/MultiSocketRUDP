#pragma once
#include "PacketManager.h"

namespace PacketHandlerUtil
{
	template <typename PacketType>
	PacketHandler MakePacketHandler(PacketHandler targetFunction)
	{
		return [targetFunction](RUDPSession& session, IPacket& inPacket)
		{
			static_assert(std::is_base_of<IPacket, PacketType>::value, "MakePacketHandler() : PacketType must inherit from IPacket");

			auto* packet = static_cast<PacketType*>(&inPacket);
			return targetFunction(session, *packet);
		};
	}

	template <typename PacketType>
	PacketHandler WappingHandler(bool (*targetFunction)(RUDPSession&, PacketType&))
	{
		return [targetFunction](RUDPSession& session, IPacket& inPacket) -> bool
		{
			auto* packet = static_cast<PacketType*>(&inPacket);
			return targetFunction(session, *packet);
		};
	}

	template <typename PacketType>
	void RegisterPacket(bool (*targetFunction)(RUDPSession&, PacketType&))
	{
		PacketHandler handler = PacketHandlerUtil::MakePacketHandler<PacketType>(PacketHandlerUtil::WappingHandler(targetFunction));
		REGISTER_PACKET(PacketType, handler);
	}

	class EssentialHandler
	{
	private:
		EssentialHandler() = default;

	public:
		static EssentialHandler& GetInst();

		static bool IsRegisteredConnectHandler() { return isRegisteredConnectHandler; }
		static bool IsRegisteredDisconnectHandler() { return isRegisteredDisconnectHandler; }

		static bool IsRegisteredAllEssentialHandler();

	public:
		template <typename PacketType>
		static void RegisterConnectHandler(bool (*targetFunction)(RUDPSession&, PacketType&))
		{
			if (isRegisteredConnectHandler == true)
			{
				std::cout << "Connect handler already registered" << std::endl;
				return;
			}

			isRegisteredConnectHandler = true;
			RegisterPacket(targetFunction);
		}

		template <typename PacketType>
		static void RegisterDisconnectHandler(bool (*targetFunction)(RUDPSession&, PacketType&))
		{
			if (isRegisteredDisconnectHandler == true)
			{
				std::cout << "Disconnect handler already registered" << std::endl;
				return;
			}

			isRegisteredDisconnectHandler = true;
			RegisterPacket(targetFunction);
		}

	private:
		inline static bool isRegisteredConnectHandler{ false };
		inline static bool isRegisteredDisconnectHandler{ false };
	};
}