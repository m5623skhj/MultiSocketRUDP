#include "PreCompile.h"
#include <iostream>
#include "PacketHandler.h"
#include "RegisterEssentialHandler.h"

int main()
{
	ContentsPacketHandler::Init();
	EssentialHandler::RegisterAllEssentialHandler();

	return 0;
}