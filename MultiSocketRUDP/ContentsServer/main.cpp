#include "PreCompile.h"
#include "TestServer.h"
#include "PlayerPacketHandlerRegister.h"
#include "Player.h"
#include <Windows.h>
#include "LogExtension.h"
#include "ServerConsole.h"
#include "Logger.h"

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

		const auto traceStats = Player::DrainTraceStats(); // TEMP_TPS_TRACE
		const auto log = Logger::MakeLogObject<ServerLog>(); // TEMP_TPS_TRACE
		log->logString = std::format(
			"[TEMP_TPS_TRACE][SERVER_SUMMARY] players={} connected={} disconnected={} retransDisconnect={} tps={} "
			"pingRecv={} pongSent={} errors={}",
			serverStats.player.load(),
			serverStats.connected.load(),
			serverStats.disconnected.load(),
			serverStats.retrans.load(),
			serverStats.tps.load(),
			traceStats.pingRecvCount,
			traceStats.pongSendCount,
			serverStats.error.load());
		Logger::GetInstance().WriteLog(log);

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
