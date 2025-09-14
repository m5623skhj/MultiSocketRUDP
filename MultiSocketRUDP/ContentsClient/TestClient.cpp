#include "PreCompile.h"
#include "TestClient.h"
#include "RUDPClientCore.h"
#include "Logger.h"
#include "LogExtension.h"
#include "Protocol.h"

bool TestClient::Start(const std::wstring& clientCoreOptionFile, const std::wstring& sessionGetterOptionFile, const bool printLogToConsole)
{
	if (not RUDPClientCore::Start(clientCoreOptionFile, sessionGetterOptionFile, printLogToConsole))
	{
		std::cout << "Core start failed" << '\n';
		return false;
	}

	// Waiting maximumConnectWaitingCount seconds
	constexpr unsigned int maximumConnectWaitingCount = 20;
	if (not WaitingConnectToServer(maximumConnectWaitingCount))
	{
		std::cout << "It was waiting " << maximumConnectWaitingCount << "seconds. But connect to server failed" << '\n';
		return false;
	}

	std::cout << "Client is running" << '\n';

	testThread = std::thread{ &TestClient::RunTestThread, this };
	return true;
}

void TestClient::Stop()
{
	RUDPClientCore::Stop();
	testThread.join();

	std::cout << "Client stopped" << '\n';
}

bool TestClient::WaitingConnectToServer(const unsigned int maximumConnectWaitingCount) const
{
	unsigned int connectWaitingCount = 0;
	while (not IsConnected())
	{
		Sleep(1000);
		++connectWaitingCount;

		if (connectWaitingCount >= maximumConnectWaitingCount)
		{
			LOG_ERROR("Waiting connect to server timeout");
			return false;
		}
	}

	return true;
}

void TestClient::RunTestThread()
{
	constexpr int firstSendCount = 5;
	SendAnyPacket(firstSendCount);

	while (not IsStopped())
	{
		constexpr unsigned int maxProcessPacketInOneTick = 100;
		int remainPacketSize = static_cast<int>(min(GetRemainPacketSize(), maxProcessPacketInOneTick));
		while (--remainPacketSize >= 0)
		{
			const auto buffer = GetReceivedPacket();
			if (buffer == nullptr)
			{
				continue;
			}

			PACKET_ID packetId;
			*buffer >> packetId;
			if (not ProcessPacketHandle(*buffer, packetId))
			{
				LOG_ERROR(std::format("ProcessPacketHandle failed by invalid packet id {}", static_cast<unsigned int>(packetId)));
			}

			NetBuffer::Free(buffer);
		}
	}

	std::cout << "Test thread stopped" << '\n';
}
