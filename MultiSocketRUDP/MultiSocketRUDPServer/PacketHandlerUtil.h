#pragma once
#include "PacketManager.h"
#include <map>

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
		EssentialHandler();

		enum EssentialHandlerType
		{
			ConnectHandlerType,
			DisconnectHandlerType,
		};

	public:
		static EssentialHandler& GetInst();

		static bool IsRegisteredAllEssentialHandler();
		static void PrintUnregisteredEssentialHandler();

	public:
		template <typename PacketType>
		static void RegisterEssentialHandler(bool (*targetFunction)(RUDPSession&, PacketType&), EssentialHandlerType targetType)
		{
			auto itor = essentialHandlerChecker.find(targetType);
			if (itor = essentialHandlerChecker.end())
			{
				std::cout << "You need register essential handler checker in constructor" << std::endl;
				return;
			}

			if (itor->second->isRegistered())
			{
				std::cout << "Connect handler already registered" << std::endl;
				return;
			}

			itor->second->isRegistered = true;
			RegisterPacket(targetFunction);
		}

		template <typename PacketType>
		static void RegisterConnectHandler(bool (*targetFunction)(RUDPSession&, PacketType&))
		{
			RegisterEssentialHandler(targetFunction, EssentialHandlerType::ConnectHandlerType);
		}

		template <typename PacketType>
		static void RegisterDisconnectHandler(bool (*targetFunction)(RUDPSession&, PacketType&))
		{
			RegisterEssentialHandler(targetFunction, EssentialHandlerType::DisconnectHandlerType);
		}

	private:
		struct EssentialHandlerRegisterChecker
		{
			friend EssentialHandler;

			EssentialHandlerRegisterChecker()
				: isRegistered(false)
			{
			}

			const bool IsRegisteredHandler() { return isRegistered; }
			virtual std::string GetHandlerType() = 0;

		private:
			bool isRegistered{ false };
		};

		struct ConnectHandlerRegisterChecker : EssentialHandlerRegisterChecker
		{
			std::string GetHandlerType() override { return "connect handler"; }
		};

		struct DisconnectHandlerRegisterChecker : EssentialHandlerRegisterChecker
		{
			std::string GetHandlerType() override { return "disconnect handler"; }
		};

	private:
		static std::map<EssentialHandlerType, std::unique_ptr<EssentialHandlerRegisterChecker>> essentialHandlerChecker;
	};
}