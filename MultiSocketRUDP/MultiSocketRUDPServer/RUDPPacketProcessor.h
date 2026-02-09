#pragma once
#include <NetServerSerializeBuffer.h>
#include <span>

class RUDPSession;
class RUDPSessionManager;

class RUDPPacketProcessor
{
public:
    RUDPPacketProcessor(RUDPSessionManager& inSessionManager);
    ~RUDPPacketProcessor() = default;

    RUDPPacketProcessor(const RUDPPacketProcessor&) = delete;
    RUDPPacketProcessor& operator=(const RUDPPacketProcessor&) = delete;

public:
    void ProcessByPacketType(RUDPSession& session, const sockaddr_in& clientAddr, NetBuffer& recvPacket) const;
    void OnRecvPacket(RUDPSession& session, NetBuffer& buffer, std::span<const unsigned char> clientAddrBuffer) const;

    [[nodiscard]]
    static WORD GetPayloadLength(const NetBuffer& buffer);

private:
	RUDPSessionManager& sessionManager;
};