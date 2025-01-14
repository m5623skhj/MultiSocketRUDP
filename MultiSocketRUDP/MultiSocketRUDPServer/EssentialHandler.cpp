#include "PreCompile.h"
#include "EssentialHandler.h"
#include "RUDPSession.h"
#include "LogExtension.h"
#include "Logger.h"

EssentialHandlerManager::EssentialHandlerManager()
{
	essentialHandler.insert({ EssentialHandlerType::OnConnectedHandlerType, std::make_pair(std::make_unique<OnConnectedHandlerRegisterChecker>(), nullptr) });
	essentialHandler.insert({ EssentialHandlerType::OnDisconnectedHandlerType, std::make_pair(std::make_unique<OnDisconnectedHandlerRegisterChecker>(), nullptr) });
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

		auto log = std::make_shared<ServerLog>();
		log->logString = checker.second.first->GetHandlerType() + " is unregistered";
		Logger::GetInstance().WriteLog(log);
	}
}

bool EssentialHandlerManager::CallRegisteredHandler(RUDPSession& session, EssentialHandlerType handlerType)
{
	return essentialHandler[handlerType].second(session);
}