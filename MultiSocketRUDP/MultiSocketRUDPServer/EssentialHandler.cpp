#include "PreCompile.h"
#include "EssentialHandler.h"
#include "RUDPSession.h"

EssentialHandlerManager::EssentialHandlerManager()
{
	essentialHandler.insert({ EssentialHandlerType::ConnectHandlerType, std::make_pair(std::make_unique<ConnectHandlerRegisterChecker>(), nullptr) });
	essentialHandler.insert({ EssentialHandlerType::DisconnectHandlerType, std::make_pair(std::make_unique<DisconnectHandlerRegisterChecker>(), nullptr) });
}

EssentialHandlerManager& EssentialHandlerManager::GetInst()
{
	static EssentialHandlerManager instance;
	return instance;
}

bool EssentialHandlerManager::IsRegisteredAllEssentialHandler()
{
	for (const auto& checker : essentialHandler)
	{
		if (not checker.second.first->IsRegisteredHandler())
		{
			return false;
		}
	}

	return true;
}

void EssentialHandlerManager::PrintUnregisteredEssentialHandler()
{
	for (const auto& checker : essentialHandler)
	{
		if (checker.second.first->IsRegisteredHandler())
		{
			continue;
		}

		std::cout << checker.second.first->GetHandlerType() << " is unregistered" << std::endl;
	}
}

bool EssentialHandlerManager::CallRegisteredHandler(RUDPSession& session, EssentialHandlerType handlerType)
{
	return essentialHandler[handlerType].second(session);
}