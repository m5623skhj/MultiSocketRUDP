#include "PreCompile.h"
#include "EssentialHandler.h"
#include "RUDPSession.h"
#include "LogExtension.h"
#include "Logger.h"

EssentialHandlerManager::EssentialHandlerManager()
{
	essentialHandler.insert({ ON_CONNECTED_HANDLER_TYPE, std::make_pair(std::make_unique<OnConnectedHandlerRegisterChecker>(), nullptr) });
	essentialHandler.insert({ ON_DISCONNECTED_HANDLER_TYPE, std::make_pair(std::make_unique<OnDisconnectedHandlerRegisterChecker>(), nullptr) });
}

EssentialHandlerManager& EssentialHandlerManager::GetInst()
{
	static EssentialHandlerManager instance;
	return instance;
}

bool EssentialHandlerManager::IsRegisteredAllEssentialHandler()
{
	return std::ranges::all_of(essentialHandler | std::views::values, [](const auto& checker)
	{
		return checker.first->IsRegisteredHandler();
	});
}

void EssentialHandlerManager::PrintUnregisteredEssentialHandler()
{
	for (const auto& [checker, _] : essentialHandler | std::views::values)
	{
		if (checker->IsRegisteredHandler())
		{
			continue;
		}

		LOG_ERROR(std::format("{} is not registered", checker->GetHandlerType()));
	}
}

bool EssentialHandlerManager::CallRegisteredHandler(RUDPSession& session, const ESSENTIAL_HANDLER_TYPE handlerType)
{
	return essentialHandler[handlerType].second(session);
}
