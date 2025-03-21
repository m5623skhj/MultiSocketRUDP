#include "PreCompile.h"
#include "PacketHandler.h"
#include "RegisterEssentialHandler.h"
#include "MultiSocketRUDPCore.h"
#include "TestServer.h"

int main()
{
	ContentsPacketHandler::Init();
	EssentialHandler::RegisterAllEssentialHandler();

	if (not TestServer::GetInst().Start(L"ServerOptionFile/CoreOption.txt", L"ServerOptionFile/SessionBrokerOption.txt"))
	{
		std::cout << "StartServer() failed" << std::endl;
		return 0;
	}
	std::cout << "Exit : ESC" << std::endl;

	while (true)
	{
		Sleep(1000);
		
		if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
		{
			TestServer::GetInst().Stop();
			break;
		}
	}

	while (not TestServer::GetInst().IsServerStopped())
	{
		Sleep(1000);
	}
	std::cout << "Server stopped" << std::endl;

	return 0;
}