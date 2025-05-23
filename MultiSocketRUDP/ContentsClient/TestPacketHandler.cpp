#include "PreCompile.h"
#include "TestClient.h"
#include "Protocol.h"
#include "RUDPClientCore.h"
#include "Logger.h"
#include "LogExtention.h"
#include <random>

namespace
{
	constexpr int maxRandomStringSize = 20;

	std::string gen_random(const int len)
	{
		static const char alphanum[] =
			"0123456789"
			"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			"abcdefghijklmnopqrstuvwxyz";

		std::random_device rd;
		std::mt19937 generator(rd());
		std::uniform_int_distribution<> dist(0, sizeof(alphanum) - 2);

		std::string tmp_s;
		tmp_s.reserve(len);

		for (int i = 0; i < len; ++i)
		{
			tmp_s += alphanum[dist(generator)];
		}

		return tmp_s;
	}

	std::string MakeRandomString()
	{
		std::random_device rd;
		std::mt19937 gen(rd());
		std::uniform_int_distribution<> distr(10, maxRandomStringSize);
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
	case PACKET_ID::Pong:
	{
	}
	break;
	case PACKET_ID::TestPacketRes:
	{
		int recvOrder;
		buffer >> recvOrder;

		{
			std::scoped_lock lock(orderListLock);
			int frontOrder = orderList.front();
			if (frontOrder != recvOrder)
			{
				g_Dump.Crash();
			}

			orderList.pop_front();
		}
	}
	break;
	case PACKET_ID::TestStringPacketRes:
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
		auto log = Logger::MakeLogObject<ClientLog>();
		log->logString = std::format("Invalid packet id {}", static_cast<unsigned int>(packetId));
		Logger::GetInstance().WriteLog(log);

		return false;
	}
	}

	SendAnyPacket();

	return true;
}

void TestClient::SendAnyPacket()
{
	static unsigned long long packetSendCount = 0;
	constexpr int pickablePacketSize = 1;
	int pickedItem = rand() % pickablePacketSize;

	switch (pickedItem)
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
		std::cout << "Invalid pickedItem" << std::endl;
		return;
	}
	}

	++packetSendCount;
	std::cout << "Packet send count " << packetSendCount << std::endl;
}

void TestClient::SendAnyPacket(const unsigned int sendCount)
{
	for (unsigned int i = 0; i < sendCount; ++i)
	{
		SendAnyPacket();
	}
}
