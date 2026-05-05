#include "PreCompile.h"
#include "TestServer.h"
#include "PlayerPacketHandlerRegister.h"
#include <Windows.h>
#include "ServerConsole.h"

int main()
{
	ContentsPacketRegister::Init();

	if (not TestServer::GetInst().Start(L"ServerOptionFile/CoreOption.txt", L"ServerOptionFile/SessionBrokerOption.txt"))
	{
		std::cout << "StartServer() failed" << '\n';
		return 0;
	}
	Sleep(3000);
	system("cls");

	EnableANSI();
	HideCursor();
	DrawUI();

	while (true)
	{
		Sleep(1000);
		
		if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
		{
			TestServer::GetInst().Stop();
			break;
		}

		serverStats.player = TestServer::GetInst().GetNumOfPlayers();
		serverStats.connected = TestServer::GetInst().GetNumOfConnected();
		serverStats.disconnected = TestServer::GetInst().GetNumOfDisconnected();
		serverStats.retrans = TestServer::GetInst().GetNumOfDisconnectedByRetransmssion();
		serverStats.tps = TestServer::GetInst().GetTPS();
		++serverStats.loopCount;
		serverStats.tpsAverage = (serverStats.tpsAverage * (serverStats.loopCount - 1) + serverStats.tps.load()) / serverStats.loopCount;
		serverStats.error = TestServer::GetInst().GetNumOfError();
		UpdateUI(40);

		TestServer::GetInst().ResetTPS();
	}

	system("cls");
	std::cout << "Server stopped" << '\n';

	return 0;
}
