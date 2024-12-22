#include "PreCompile.h"
#include "MultiSocketRUDPCore.h"

bool MultiSocketRUDPCore::ProcessByPacketType(std::shared_ptr<RUDPSession> session, const sockaddr_in& clientAddr, NetBuffer& recvPacket)
{
	PACKET_TYPE packetType;
	recvPacket >> packetType;

	switch (packetType)
	{
	case PACKET_TYPE::ConnectType:
	{
		session->TryConnect(recvPacket);
		break;
	}
	break;
	case PACKET_TYPE::DisconnectType:
	{
		if (not session->CheckMyClient(clientAddr))
		{
			break;
		}

		session->Disconnect(recvPacket);
		return false;
	}
	break;
	case PACKET_TYPE::SendType:
	{
		if (not session->CheckMyClient(clientAddr))
		{
			break;
		}

		if (session->OnRecvPacket(recvPacket) == false)
		{
			session->Disconnect();
		}
		break;
	}
	break;
	case PACKET_TYPE::SendReplyType:
	{
		if (not session->CheckMyClient(clientAddr))
		{
			break;
		}

		session->OnSendReply(recvPacket);
		break;
	}
	break;
	default:
		// TODO : Write log
		break;
	}

	return true;
}
