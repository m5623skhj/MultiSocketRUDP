#pragma once
#include <NetServerSerializeBuffer.h>
#include <span>

class RUDPSession;
class RUDPSessionManager;

// ----------------------------------------
// @brief RUDP 패킷을 유형별로 처리하는 클래스입니다.
// ----------------------------------------
class RUDPPacketProcessor
{
public:
    RUDPPacketProcessor(RUDPSessionManager& inSessionManager);
    ~RUDPPacketProcessor() = default;

    RUDPPacketProcessor(const RUDPPacketProcessor&) = delete;
    RUDPPacketProcessor& operator=(const RUDPPacketProcessor&) = delete;

public:
    // ----------------------------------------
    // @brief 수신된 패킷의 유형에 따라 적절한 처리를 수행합니다.
    // @param session 패킷을 수신한 RUDPSession 객체
    // @param clientAddr 클라이언트 주소 정보
    // @param recvPacket 수신된 NetBuffer 패킷
    // ----------------------------------------
    void ProcessByPacketType(RUDPSession& session, const sockaddr_in& clientAddr, NetBuffer& recvPacket) const;
    // ----------------------------------------
    // @brief RIO 완료 포트에서 수신된 패킷을 처리합니다.
    // @param session 패킷을 수신한 RUDPSession 객체
    // @param buffer 수신된 NetBuffer
    // @param clientAddrBuffer 클라이언트 주소 정보가 담긴 버퍼
    // ----------------------------------------
    void OnRecvPacket(RUDPSession& session, NetBuffer& buffer, std::span<const unsigned char> clientAddrBuffer) const;

    // ----------------------------------------
    // @brief NetBuffer에서 페이로드 길이를 추출합니다.
    // @param buffer 페이로드 길이를 포함하는 NetBuffer 객체
    // @return 페이로드 길이 (WORD 형식)
    // ----------------------------------------
    [[nodiscard]]
    static WORD GetPayloadLength(const NetBuffer& buffer);

private:
	RUDPSessionManager& sessionManager;
};