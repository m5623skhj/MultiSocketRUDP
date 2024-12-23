#include "PreCompile.h"
#include <iostream>
#include "PacketHandler.h"
#include "RegisterEssentialHandler.h"
#include "MultiSocketRUDPCore.h"

int main()
{
	ContentsPacketHandler::Init();
	EssentialHandler::RegisterAllEssentialHandler();

	MultiSocketRUDPCore serverCore;
	if (not serverCore.StartServer(L"OptionFile/CoreOption.txt", L"OptionFile/SessionBrokerOption.txt"))
	{
		std::cout << "StartServer() failed" << std::endl;
		return 0;
	}

	while (true)
	{
		Sleep(1000);
		
		if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
		{
			serverCore.StopServer();
			break;
		}
	}
	std::cout << "Server stop" << std::endl;

	return 0;
}