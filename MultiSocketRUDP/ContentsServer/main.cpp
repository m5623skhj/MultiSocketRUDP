#include "PreCompile.h"
#include "MultiSocketRUDPCore.h"
#include "TestServer.h"
#include "PlayerPacketHandlerRegister.h"

int main()
{
	ContentsPacketRegister::Init();

	if (not TestServer::GetInst().Start(L"ServerOptionFile/CoreOption.txt", L"ServerOptionFile/SessionBrokerOption.txt"))
	{
		std::cout << "StartServer() failed" << '\n';
		return 0;
	}
	std::cout << "Exit : ESC" << '\n';

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
	std::cout << "Server stopped" << '\n';

	return 0;
}
