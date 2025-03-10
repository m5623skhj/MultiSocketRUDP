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
	if (not RUDPClientCore::GetInst().Start(clientCoreOptionFile, sessionGetterOptionFile, true))
	{
		std::cout << "Core start failed" << std::endl;
		return false;
	}

	testThread = std::thread{ &TestClient::RunTestThread, this };

	// Waiting maximumConnectWaitingCount seconds
	constexpr unsigned int maximumConnectWaitingCount = 20;
	if (not WaitingConnectToServer(maximumConnectWaitingCount))
	{
		std::cout << "It was waiting " << maximumConnectWaitingCount << "seconds. But connect to server failed" << std::endl;
		return false;
	}

	std::cout << "Client is running" << std::endl;
	return true;
}

void TestClient::Stop()
{
	RUDPClientCore::GetInst().Stop();
	testThread.join();

	std::cout << "Client stopped" << std::endl;
}

bool TestClient::IsConnected() const
{
	return RUDPClientCore::GetInst().IsConnected();
}

bool TestClient::WaitingConnectToServer(const unsigned int maximumConnectWaitingCount)
{
	unsigned int connectWaitingCount = 0;
	while (not RUDPClientCore::GetInst().IsConnected())
	{
		Sleep(1000);
		++connectWaitingCount;

		if (connectWaitingCount >= maximumConnectWaitingCount)
		{
			auto log = Logger::MakeLogObject<ClientLog>();
			log->logString = "Connect to server failed";
			Logger::GetInstance().WriteLog(log);

			return false;
		}
	}

	return true;
}

void TestClient::RunTestThread()
{
	constexpr int firstSendCount = 5;
	SendAnyPacket(firstSendCount);

	constexpr unsigned int maxProcessPacketInOneTick = 100;
	while (not RUDPClientCore::GetInst().IsStopped())
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
				log->logString = std::format("ProcessPacketHandle failed by invalid packet id {}", log->logString += static_cast<unsigned int>(packetId));
				Logger::GetInstance().WriteLog(log);
			}

			NetBuffer::Free(buffer);
		}
	}

	std::cout << "Test thread stopped" << std::endl;
}
