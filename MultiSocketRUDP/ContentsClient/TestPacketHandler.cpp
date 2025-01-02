#include "PreCompile.h"
#include "TestClient.h"
#include "Protocol.h"

bool TestClient::ProcessPacketHandle(NetBuffer& buffer)
{
	PACKET_ID packetId;
	buffer >> packetId;

	switch (packetId)
	{
	case PACKET_ID::Pong:
	{

	}
	break;
	case PACKET_ID::TestPacketRes:
	{

	}
	break;
	default:
		return false;
	}

	return true;
}
