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

	std::thread uiThread([]()
		{
			while (true)
			{
				UpdateUI(40);
				std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			}
		});

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

		TestServer::GetInst().ResetTPS();
	}

	while (not TestServer::GetInst().IsServerStopped())
	{
		Sleep(1000);
	}
	uiThread.join();

	system("cls");
	std::cout << "Server stopped" << '\n';

	return 0;
}
