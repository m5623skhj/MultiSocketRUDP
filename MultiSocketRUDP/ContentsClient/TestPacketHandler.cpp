#include "PreCompile.h"
#include "TestClient.h"
#include "Protocol.h"
#include "RUDPClientCore.h"

bool TestClient::ProcessPacketHandle(NetBuffer& buffer)
{
	PACKET_ID packetId;
	buffer >> packetId;

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
	default:
		return false;
	}

	return true;
}
