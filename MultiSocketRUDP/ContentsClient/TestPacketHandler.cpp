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

		if (order != recvOrder)
		{
			g_Dump.Crash();
		}
	}
	break;
	case PACKET_ID::TestStringPacketRes:
	{
		std::string recvString;
		buffer >> recvString;

		if (recvString != echoString)
		{
			g_Dump.Crash();
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
	constexpr int pickablePacketSize = 3;
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
		RUDPClientCore::GetInst().SendPacket(req);
	}
	break;
	case 2:
	{
		echoString = MakeRandomString();
		TestStringPacketReq req;
		req.testString = echoString;
		RUDPClientCore::GetInst().SendPacket(req);
	}
	break;
	default:
	{
	}
	break;
	}
}

void TestClient::SendAnyPacket(const unsigned int sendCount)
{
	for (unsigned int i = 0; i < sendCount; ++i)
	{
		SendAnyPacket();
	}
}
