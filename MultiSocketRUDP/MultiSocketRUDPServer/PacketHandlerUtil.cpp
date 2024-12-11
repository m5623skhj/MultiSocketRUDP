#include "PreCompile.h"
#include "PacketHandlerUtil.h"

PacketHandlerUtil::EssentialHandler& PacketHandlerUtil::EssentialHandler::GetInst()
{
	static EssentialHandler instance;
	return instance;
}

bool PacketHandlerUtil::EssentialHandler::IsRegisteredAllEssentialHandler()
{
	bool isRegisteredAll{ true };

	isRegisteredAll &= IsRegisteredConnectHandler();
	isRegisteredAll &= IsRegisteredDisconnectHandler();

	return isRegisteredAll;
}