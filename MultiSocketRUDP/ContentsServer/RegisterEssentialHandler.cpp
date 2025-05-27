#include "PreCompile.h"
#include "EssentialHandler.h"
#include "RegisterEssentialHandler.h"
#include "PCManager.h"
#include "RUDPSession.h"
#include "PC.h"

namespace EssentialHandler
{
	bool HandleOnConnected(RUDPSession& session)
	{
		return PCManager::GetInst().InsertPC(std::make_shared<PC>(session));
	}

	bool HandleOnDisconnected(const RUDPSession& session)
	{
		return PCManager::GetInst().DeletePC(session.GetSessionId());
	}

	void RegisterAllEssentialHandler()
	{
		RUDPCoreEssentialFunction onConnectedHandler{ HandleOnConnected };
		RUDPCoreEssentialFunction onDisconnectedHandler{ HandleOnDisconnected };

		EssentialHandlerManager::GetInst().RegisterOnConnectedHandler(onConnectedHandler);
		EssentialHandlerManager::GetInst().RegisterOnDisconnectedHandler(onDisconnectedHandler);
	}
}
