#include "PreCompile.h"

#include "LogExtension.h"
#include "Logger.h"
#include "MultiSocketRUDPCore.h"
#include "../Common/PacketCrypto/PacketCryptoHelper.h"

#define DECODE_PACKET() \
if (not PacketCryptoHelper::DecodePacket(recvPacket, session.sessionSalt, SESSION_SALT_SIZE, session.sessionKeyHandle)) \
{ break; }

void MultiSocketRUDPCore::ProcessByPacketType(RUDPSession& session, const sockaddr_in& clientAddr, NetBuffer& recvPacket)
{
	PACKET_TYPE packetType;
	recvPacket >> packetType;

	session.nowInProcessingRecvPacket = true;

	switch (packetType)
	{
	case PACKET_TYPE::CONNECT_TYPE:
	{
		DECODE_PACKET()

		session.TryConnect(recvPacket, clientAddr);
		break;
	}
	case PACKET_TYPE::DISCONNECT_TYPE:
	{
		if (not session.CanProcessPacket(clientAddr))
		{
			break;
		}
		DECODE_PACKET()

		session.Disconnect(recvPacket);
		break;
	}
	case PACKET_TYPE::SEND_TYPE:
	{
		if (not session.CanProcessPacket(clientAddr))
		{
			break;
		}
		DECODE_PACKET()

		if (session.OnRecvPacket(recvPacket) == false)
		{
			session.DoDisconnect();
		}
		break;
	}
	case PACKET_TYPE::SEND_REPLY_TYPE:
	case PACKET_TYPE::HEARTBEAT_REPLY_TYPE:
	{
		if (not session.CanProcessPacket(clientAddr))
		{
			break;
		}
		DECODE_PACKET()

		session.OnSendReply(recvPacket);
		break;
	}
	default:
		LOG_ERROR(std::format("Invalid packet type received: {}", static_cast<int>(packetType)));
		break;
	}

	session.nowInProcessingRecvPacket = false;
}
