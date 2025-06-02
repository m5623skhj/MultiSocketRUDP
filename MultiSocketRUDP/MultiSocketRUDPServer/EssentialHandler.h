#pragma once
#include "PacketManager.h"
#include <unordered_map>
#include <functional>
#include "CoreType.h"

using RUDPCoreEssentialFunction = std::function<bool(RUDPSession&)>;

class EssentialHandlerManager
{
public:
	enum ESSENTIAL_HANDLER_TYPE
	{
		ON_CONNECTED_HANDLER_TYPE,
		ON_DISCONNECTED_HANDLER_TYPE,
	};

private:
	EssentialHandlerManager();

public:
	static EssentialHandlerManager& GetInst();

	static bool IsRegisteredAllEssentialHandler();
	static void PrintUnregisteredEssentialHandler();
	static bool CallRegisteredHandler(RUDPSession& session, const ESSENTIAL_HANDLER_TYPE handlerType);

private:
	static void RegisterEssentialHandler(const RUDPCoreEssentialFunction& rudpCoreEssentialFunction, const ESSENTIAL_HANDLER_TYPE targetType)
	{
		const auto itor = essentialHandler.find(targetType);
		if (itor == essentialHandler.end())
		{
			std::cout << "You need register essential handler checker in constructor" << '\n';
			return;
		}

		if (itor->second.first->IsRegisteredHandler())
		{
			std::cout << "Connect handler already registered" << '\n';
			return;
		}

		itor->second.second = rudpCoreEssentialFunction;
		itor->second.first->isRegistered = true;
	}

public:
	static void RegisterOnConnectedHandler(const RUDPCoreEssentialFunction& rudpCoreEssentialFunction)
	{
		RegisterEssentialHandler(rudpCoreEssentialFunction, ESSENTIAL_HANDLER_TYPE::ON_CONNECTED_HANDLER_TYPE);
	}

	static void RegisterOnDisconnectedHandler(const RUDPCoreEssentialFunction& rudpCoreEssentialFunction)
	{
		RegisterEssentialHandler(rudpCoreEssentialFunction, ESSENTIAL_HANDLER_TYPE::ON_DISCONNECTED_HANDLER_TYPE);
	}

private:
	struct EssentialHandlerRegisterChecker
	{
		friend EssentialHandlerManager;

		EssentialHandlerRegisterChecker() = default;
		virtual ~EssentialHandlerRegisterChecker() = default;

		bool IsRegisteredHandler() const { return isRegistered; }
		virtual std::string GetHandlerType() = 0;

	private:
		bool isRegistered{ false };
	};

	struct OnConnectedHandlerRegisterChecker final : EssentialHandlerRegisterChecker
	{
		std::string GetHandlerType() override { return "OnConnected handler"; }
	};

	struct OnDisconnectedHandlerRegisterChecker final : EssentialHandlerRegisterChecker
	{
		std::string GetHandlerType() override { return "OnDisconnected handler"; }
	};

private:
	inline static std::unordered_map<ESSENTIAL_HANDLER_TYPE, std::pair<std::unique_ptr<EssentialHandlerRegisterChecker>, RUDPCoreEssentialFunction>> essentialHandler = {};
};