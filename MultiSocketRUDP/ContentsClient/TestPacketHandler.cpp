#include "PreCompile.h"
#include "TestClient.h"
#include "Protocol.h"
#include "RUDPClientCore.h"
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

bool TestClient::ProcessPacketHandle(NetBuffer& buffer, PACKET_ID packetId)
{
	switch (packetId)
	{
	case PACKET_ID::Pong:
	{
		Ping ping;
		RUDPClientCore::GetInst().SendPacket(ping);
	}
	break;
	case PACKET_ID::TestPacketRes:
	{
		static int order = 0;

		int recvOrder;
		buffer >> recvOrder;

		if (order != recvOrder)
		{
			g_Dump.Crash();
		}

		TestPacketReq req;
		req.order = ++order;
		RUDPClientCore::GetInst().SendPacket(req);
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

		echoString = MakeRandomString();
		TestStringPacketReq req;
		req.testString = echoString;
		RUDPClientCore::GetInst().SendPacket(req);
	}
	default:
		return false;
	}

	return true;
}
