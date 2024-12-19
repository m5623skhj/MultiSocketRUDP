#include "PreCompile.h"
#include "EssentialHandler.h"
#include "RegisterEssentialHandler.h"

namespace EssentialHandler
{
	bool HandleOnConnected(RUDPSession& session)
	{
		return true;
	}

	bool HandleOnDisconnected(RUDPSession& session)
	{
		return true;
	}

	void RegisterAllEssentialHandler()
	{
		RUDPCoreEssentialFunction onConnectedHandler{ HandleOnConnected };
		RUDPCoreEssentialFunction onDisconnectedHandler{ HandleOnDisconnected };

		EssentialHandlerManager::GetInst().RegisterOnConnectedHandler(onConnectedHandler);
		EssentialHandlerManager::GetInst().RegisterOnDisconnectedHandler(onDisconnectedHandler);
	}
}
