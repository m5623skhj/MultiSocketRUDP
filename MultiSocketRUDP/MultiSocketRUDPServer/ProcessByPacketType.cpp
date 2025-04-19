#include "PreCompile.h"
#include "MultiSocketRUDPCore.h"

bool MultiSocketRUDPCore::ProcessByPacketType(RUDPSession& session, const sockaddr_in& clientAddr, NetBuffer& recvPacket)
{
	PACKET_TYPE packetType;
	recvPacket >> packetType;

	session.nowInProcessingRecvPacket = true;
	switch (packetType)
	{
	case PACKET_TYPE::ConnectType:
	{
		session.TryConnect(recvPacket, clientAddr);
		break;
	}
	break;
	case PACKET_TYPE::DisconnectType:
	{
		if (not session.CheckMyClient(clientAddr) || session.IsReleasing())
		{
			break;
		}

		session.Disconnect(recvPacket);
		session.nowInProcessingRecvPacket = false;
		return false;
	}
	break;
	case PACKET_TYPE::SendType:
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
	break;
	case PACKET_TYPE::SendReplyType:
	{
		if (not session.CheckMyClient(clientAddr) || session.IsReleasing())
		{
			break;
		}

		session.OnSendReply(recvPacket);
		break;
	}
	break;
	default:
		// TODO : Write log
		break;
	}
	session.nowInProcessingRecvPacket = false;

	return true;
}
