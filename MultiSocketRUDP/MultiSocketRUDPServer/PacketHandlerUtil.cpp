#include "PreCompile.h"
#include "PacketHandlerUtil.h"

PacketHandlerUtil::EssentialHandler::EssentialHandler()
{
	essentialHandlerChecker.insert({ EssentialHandlerType::ConnectHandlerType, std::make_unique<ConnectHandlerRegisterChecker>() });
	essentialHandlerChecker.insert({ EssentialHandlerType::DisconnectHandlerType, std::make_unique<DisconnectHandlerRegisterChecker>() });
}

PacketHandlerUtil::EssentialHandler& PacketHandlerUtil::EssentialHandler::GetInst()
{
	static EssentialHandler instance;
	return instance;
}

bool PacketHandlerUtil::EssentialHandler::IsRegisteredAllEssentialHandler()
{
	for (const auto& checker : essentialHandlerChecker)
	{
		if (not checker.second->IsRegisteredHandler())
		{
			return false;
		}
	}

	return true;
}

void PacketHandlerUtil::EssentialHandler::PrintUnregisteredEssentialHandler()
{
	for (const auto& checker : essentialHandlerChecker)
	{
		if (checker.second->IsRegisteredHandler())
		{
			continue;
		}

		std::cout << checker.second->GetHandlerType() << " is unregistered" << std::endl;
	}
}