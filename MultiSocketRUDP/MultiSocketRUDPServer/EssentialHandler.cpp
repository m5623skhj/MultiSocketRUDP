#include "PreCompile.h"
#include "EssentialHandler.h"
#include "RUDPSession.h"
#include "LogExtension.h"
#include "Logger.h"

EssentialHandlerManager::EssentialHandlerManager()
{
	essentialHandler.insert({ ESSENTIAL_HANDLER_TYPE::ON_CONNECTED_HANDLER_TYPE, std::make_pair(std::make_unique<OnConnectedHandlerRegisterChecker>(), nullptr) });
	essentialHandler.insert({ ESSENTIAL_HANDLER_TYPE::ON_DISCONNECTED_HANDLER_TYPE, std::make_pair(std::make_unique<OnDisconnectedHandlerRegisterChecker>(), nullptr) });
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

		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = checker.second.first->GetHandlerType() + " is unregistered";
		Logger::GetInstance().WriteLog(log);
	}
}

bool EssentialHandlerManager::CallRegisteredHandler(RUDPSession& session, ESSENTIAL_HANDLER_TYPE handlerType)
{
	return essentialHandler[handlerType].second(session);
}
