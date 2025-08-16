#include "PreCompile.h"
#include "TestClient.h"
#include "Protocol.h"
#include "RUDPClientCore.h"
#include "Logger.h"
#include "LogExtension.h"
#include <random>

namespace
{
	constexpr int MAX_RANDOM_STRING_SIZE = 20;

	std::string gen_random(const int len)
	{
		static const char alphanum[] =
			"0123456789"
			"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			"abcdefghijklmnopqrstuvwxyz";

		std::random_device rd;
		std::mt19937 generator(rd());
		std::uniform_int_distribution<> dist(0, sizeof(alphanum) - 2);

		std::string tmpString;
		tmpString.reserve(len);

		for (int i = 0; i < len; ++i)
		{
			tmpString += alphanum[dist(generator)];
		}

		return tmpString;
	}

	std::string MakeRandomString()
	{
		std::random_device rd;
		std::mt19937 gen(rd());
		std::uniform_int_distribution<> distr(10, MAX_RANDOM_STRING_SIZE);
		return gen_random(distr(gen));
	}
}

bool TestClient::ProcessPacketHandle(NetBuffer& buffer, const PACKET_ID packetId)
{
	if (IsConnected() == false)
	{
		return false;
	}

	switch (packetId)
	{
	case PACKET_ID::PONG:
	{
	}
	break;
	case PACKET_ID::TEST_PACKET_RES:
	{
		int recvOrder;
		buffer >> recvOrder;

		{
			std::scoped_lock lock(orderListLock);
			if (const int frontOrder = orderList.front(); frontOrder != recvOrder)
			{
				g_Dump.Crash();
			}

			orderList.pop_front();
		}
	}
	break;
	case PACKET_ID::TEST_STRING_PACKET_RES:
	{
		std::string recvString;
		buffer >> recvString;

		{
			std::scoped_lock lock(echoStringSetLock);
			if (not echoStringSet.contains(recvString))
			{
				g_Dump.Crash();
			}

			echoStringSet.erase(recvString);
		}
	}
	break;
	default:
	{
		LOG_ERROR(std::format("Invalid packet id received: {}", static_cast<unsigned int>(packetId)));
		return false;
	}
	}

	SendAnyPacket();

	return true;
}

void TestClient::SendAnyPacket()
{
	static unsigned long long packetSendCount = 0;
	switch (constexpr int pickAblePacketSize = 1; rand() % pickAblePacketSize)
	{
	case 0:
	{
		Ping ping;
		RUDPClientCore::GetInst().SendPacket(ping);
	}
	break;
	case 1:
	{
		TestPacketReq req;
		
		req.order = ++order;
		{
			std::scoped_lock lock(orderListLock);
			orderList.emplace_back(req.order);
		}
		RUDPClientCore::GetInst().SendPacket(req);
	}
	break;
	case 2:
	{
		auto echoString = MakeRandomString();
		TestStringPacketReq req;
		req.testString = echoString;
		{
			std::scoped_lock lock(echoStringSetLock);
			echoStringSet.emplace(echoString);
		}
		RUDPClientCore::GetInst().SendPacket(req);
	}
	break;
	default:
	{
		std::cout << "Invalid pickedItem" << '\n';
		return;
	}
	}

	++packetSendCount;
	std::cout << "Packet send count " << packetSendCount << '\n';
}

void TestClient::SendAnyPacket(const unsigned int sendCount)
{
	for (unsigned int i = 0; i < sendCount; ++i)
	{
		SendAnyPacket();
	}
}
