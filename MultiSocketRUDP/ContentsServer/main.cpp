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

	int previousConnectedCount = 0;

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
		serverStats.error = TestServer::GetInst().GetNumOfError();

		const int connectedCount = serverStats.connected.load();
		if (previousConnectedCount == 0 && connectedCount > 0)
		{
			serverStats.tpsAverage.store(0, std::memory_order_relaxed);
			serverStats.tpsAverageSampleCount = 0;
		}

		if (connectedCount > 0)
		{
			++serverStats.tpsAverageSampleCount;
			serverStats.tpsAverage =
				(serverStats.tpsAverage * (serverStats.tpsAverageSampleCount - 1) + serverStats.tps.load()) /
				serverStats.tpsAverageSampleCount;
		}

		previousConnectedCount = connectedCount;
		UpdateUI(40);

		TestServer::GetInst().ResetTPS();
	}

	system("cls");
	std::cout << "Server stopped" << '\n';

	return 0;
}
