#pragma once
#include "PacketManager.h"
#include <unordered_map>
#include <functional>
#include "CoreType.h"

using RUDPCoreEssentialFunction = std::function<bool(RUDPSession&)>;

class EssentialHandlerManager
{
public:
	enum EssentialHandlerType
	{
		ConnectHandlerType,
		DisconnectHandlerType,
	};

private:
	EssentialHandlerManager();

public:
	static EssentialHandlerManager& GetInst();

	static bool IsRegisteredAllEssentialHandler();
	static void PrintUnregisteredEssentialHandler();
	static bool CallRegisteredHandler(RUDPSession& session, EssentialHandlerType handlerType);

private:
	static void RegisterEssentialHandler(RUDPCoreEssentialFunction& rudpCoreEssentialFunction, EssentialHandlerType targetType)
	{
		auto itor = essentialHandler.find(targetType);
		if (itor == essentialHandler.end())
		{
			std::cout << "You need register essential handler checker in constructor" << std::endl;
			return;
		}

		if (itor->second.first->IsRegisteredHandler())
		{
			std::cout << "Connect handler already registered" << std::endl;
			return;
		}

		itor->second.second = rudpCoreEssentialFunction;
		itor->second.first->isRegistered = true;
	}

public:
	static void RegisterConnectHandler(RUDPCoreEssentialFunction& rudpCoreEssentialFunction)
	{
		RegisterEssentialHandler(rudpCoreEssentialFunction, EssentialHandlerType::ConnectHandlerType);
	}

	static void RegisterDisconnectHandler(RUDPCoreEssentialFunction& rudpCoreEssentialFunction)
	{
		RegisterEssentialHandler(rudpCoreEssentialFunction, EssentialHandlerType::DisconnectHandlerType);
	}

private:
	struct EssentialHandlerRegisterChecker
	{
		friend EssentialHandlerManager;

		EssentialHandlerRegisterChecker() = default;

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
	inline static std::unordered_map<EssentialHandlerType, std::pair<std::unique_ptr<EssentialHandlerRegisterChecker>, RUDPCoreEssentialFunction>> essentialHandler = {};
};