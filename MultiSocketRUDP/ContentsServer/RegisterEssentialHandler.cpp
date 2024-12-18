#include "PreCompile.h"
#include "EssentialHandler.h"
#include "RegisterEssentialHandler.h"

namespace EssentialHandler
{
	bool HandleConnect(RUDPSession& session)
	{
		return true;
	}

	bool HandleDisconnect(RUDPSession& session)
	{
		return true;
	}

	void RegisterAllEssentialHandler()
	{
		RUDPCoreEssentialFunction connectHandler{ HandleConnect };
		RUDPCoreEssentialFunction disconnectHandler{ HandleDisconnect };

		EssentialHandlerManager::GetInst().RegisterConnectHandler(connectHandler);
		EssentialHandlerManager::GetInst().RegisterDisconnectHandler(disconnectHandler);
	}
}
