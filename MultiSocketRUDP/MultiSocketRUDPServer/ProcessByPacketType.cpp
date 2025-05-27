#include "PreCompile.h"
#include "MultiSocketRUDPCore.h"

bool MultiSocketRUDPCore::ProcessByPacketType(RUDPSession& session, const sockaddr_in& clientAddr, NetBuffer& recvPacket)
{
	PACKET_TYPE packetType;
	recvPacket >> packetType;

	session.nowInProcessingRecvPacket = true;
	switch (packetType)
	{
	case PACKET_TYPE::CONNECT_TYPE:
	{
		session.TryConnect(recvPacket, clientAddr);
		break;
	}
	case PACKET_TYPE::DISCONNECT_TYPE:
	{
		if (not session.CheckMyClient(clientAddr) || session.IsReleasing())
		{
			break;
		}

		session.Disconnect(recvPacket);
		session.nowInProcessingRecvPacket = false;
		return false;
	}
	case PACKET_TYPE::SEND_TYPE:
	{
		if (not session.CheckMyClient(clientAddr) || session.IsReleasing())
		{
			break;
		}

		if (session.OnRecvPacket(recvPacket) == false)
		{
			session.Disconnect();
			session.nowInProcessingRecvPacket = false;
			return false;
		}
		break;
	}
	case PACKET_TYPE::SEND_REPLY_TYPE:
	case PACKET_TYPE::HEARTBEAT_REPLY_TYPE:
	{
		if (not session.CheckMyClient(clientAddr) || session.IsReleasing())
		{
			break;
		}

		session.OnSendReply(recvPacket);
		break;
	}
	default:
		// TODO : Write log
		break;
	}
	session.nowInProcessingRecvPacket = false;

	return true;
}
