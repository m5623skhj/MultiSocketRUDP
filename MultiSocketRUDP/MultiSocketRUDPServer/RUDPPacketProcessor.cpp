#include "PreCompile.h"
#include "RUDPPacketProcessor.h"
#include "MultiSocketRUDPCore.h"
#include "RUDPSession.h"
#include "RUDPSessionManager.h"
#include "LogExtension.h"
#include "Logger.h"
#include "RUDPSessionFunctionDelegate.h"
#include "../Common/PacketCrypto/PacketCryptoHelper.h"
#include "ISessionDelegate.h"

#define DECODE_PACKET() \
    if (not PacketCryptoHelper::DecodePacket(recvPacket, sessionSalt, SESSION_SALT_SIZE, sessionKeyHandle, isCorePacket, direction)) \
    { break; }

RUDPPacketProcessor::RUDPPacketProcessor(RUDPSessionManager& inSessionManager
	, ISessionDelegate& inSessionDelegate)
	: sessionManager(inSessionManager)
	, sessionDelegate(inSessionDelegate)
{
}

void RUDPPacketProcessor::ProcessByPacketType(RUDPSession& session, const sockaddr_in& clientAddr, NetBuffer& recvPacket)
{
    PACKET_TYPE packetType;
    recvPacket >> packetType;

    bool isCorePacket = true;
    auto direction = PACKET_DIRECTION::CLIENT_TO_SERVER;

    const auto sessionKeyHandle = sessionDelegate.GetSessionKeyHandle(session);
	const auto sessionSalt = sessionDelegate.GetSessionSalt(session);
	if (sessionKeyHandle == nullptr || sessionSalt == nullptr)
	{
		LOG_ERROR("Session key or salt is nullptr in RUDPPacketProcessor::ProcessByPacketType()");
		return;
	}

    switch (packetType)
    {
    case PACKET_TYPE::CONNECT_TYPE:
    {
        DECODE_PACKET()

        if (sessionDelegate.TryConnect(session, recvPacket, clientAddr))
        {
			sessionManager.IncrementConnectedCount();
        }
        break;
    }
    case PACKET_TYPE::DISCONNECT_TYPE:
    {
        if (not sessionDelegate.CanProcessPacket(session, clientAddr))
        {
            break;
        }
        DECODE_PACKET()

   		sessionDelegate.Disconnect(session, recvPacket);
        break;
    }
    case PACKET_TYPE::SEND_TYPE:
    {
        if (not sessionDelegate.CanProcessPacket(session, clientAddr))
        {
            break;
        }
        isCorePacket = false;
        DECODE_PACKET()

        if (sessionDelegate.OnRecvPacket(session, recvPacket))
        {
            tps.fetch_add(1, std::memory_order_relaxed);
        }
        else
        {
            session.DoDisconnect();
        }
        break;
    }
    case PACKET_TYPE::SEND_REPLY_TYPE:
    case PACKET_TYPE::HEARTBEAT_REPLY_TYPE:
    {
        if (not sessionDelegate.CanProcessPacket(session, clientAddr))
        {
            break;
        }

        direction = PACKET_DIRECTION::CLIENT_TO_SERVER_REPLY;
        DECODE_PACKET()

		sessionDelegate.OnSendReply(session, recvPacket);
        break;
    }
    default:
        LOG_ERROR(std::format("Invalid packet type received: {}", static_cast<int>(packetType)));
        break;
    }
}

void RUDPPacketProcessor::OnRecvPacket(RUDPSession& session, NetBuffer& buffer, const std::span<const unsigned char> clientAddrBuffer)
{
    if (buffer.GetUseSize() != GetPayloadLength(buffer))
    {
        return;
    }

    if (clientAddrBuffer.size() < sizeof(sockaddr_in))
    {
        return;
    }

    sockaddr_in clientAddr;
    std::ignore = memcpy_s(&clientAddr, sizeof(clientAddr), clientAddrBuffer.data(), sizeof(clientAddr));
    ProcessByPacketType(session, clientAddr, buffer);
}

WORD RUDPPacketProcessor::GetPayloadLength(const NetBuffer& buffer)
{
    return MultiSocketRUDPCore::GetPayloadLength(buffer);
}

int32_t RUDPPacketProcessor::GetTPS() const
{
    return tps.load(std::memory_order_relaxed);
}

void RUDPPacketProcessor::ResetTPS()
{
    tps.store(0, std::memory_order_relaxed);
}
