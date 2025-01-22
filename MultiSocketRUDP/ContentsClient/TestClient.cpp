#include "PreCompile.h"
#include "TestClient.h"
#include "RUDPClientCore.h"
#include "Logger.h"
#include "LogExtention.h"
#include "Protocol.h"

TestClient& TestClient::GetInst()
{
	static TestClient instance;
	return instance;
}

bool TestClient::Start(const std::wstring& clientCoreOptionFile, const std::wstring& sessionGetterOptionFile)
{
	if (not RUDPClientCore::GetInst().Start(clientCoreOptionFile, sessionGetterOptionFile, false))
	{
		std::cout << "Core start failed" << std::endl;
		return false;
	}

	testThread = std::thread{ &TestClient::RunTestThread, this };

	std::cout << "Client is running" << std::endl;
	return true;
}

void TestClient::Stop()
{
	RUDPClientCore::GetInst().Stop();
	testThread.join();

	std::cout << "Client stopped" << std::endl;
}

void TestClient::RunTestThread()
{
	const unsigned int maxProcessPacketInOneTick = 100;

	while (not RUDPClientCore::GetInst().IsConnected())
	{
		Sleep(1000);
	}
	{
		Ping ping;
		RUDPClientCore::GetInst().SendPacket(ping);
	}

	while (RUDPClientCore::GetInst().IsStopped())
	{
		auto remainPacketSize = min(RUDPClientCore::GetInst().GetRemainPacketSize(), maxProcessPacketInOneTick);
		while (remainPacketSize > 0)
		{
			auto buffer = RUDPClientCore::GetInst().GetReceivedPacket();
			if (buffer == nullptr)
			{
				continue;
			}

			PACKET_ID packetId;
			*buffer >> packetId;
			if (not ProcessPacketHandle(*buffer, packetId))
			{
				auto log = Logger::MakeLogObject<ClientLog>();
				log->logString = "ProcessPacketHandle failed by invalid packet id ";
				log->logString += static_cast<unsigned int>(packetId);
				Logger::GetInstance().WriteLog(log);
			}

			NetBuffer::Free(buffer);
		}
	}

	std::cout << "Test thread stopped" << std::endl;
}
