# RUDPPacketProcessor

> **RecvLogic Worker Thread에서 호출되는 패킷 타입 분류기.**  
> `NetBuffer`를 `PACKET_TYPE`에 따라 해당 세션 메서드로 라우팅하고,  
> SEND_TYPE 처리마다 TPS 카운터를 증가시킨다.

---

## 목차

1. [위치와 호출 경로](#1-위치와-호출-경로)
2. [OnRecvPacket — 사전 유효성 검사](#2-onrecvpacket--사전-유효성-검사)
3. [ProcessByPacketType — 타입별 분기 전체](#3-processbypackettype--타입별-분기-전체)
4. [DECODE_PACKET 매크로](#4-decode_packet-매크로)
5. [CanProcessPacket — 스푸핑 방지](#5-canprocesspacket--스푸핑-방지)
6. [TPS 카운터](#6-tps-카운터)
7. [의존성 및 생성자](#7-의존성-및-생성자)

---

## 1. 위치와 호출 경로

```
[RecvLogic Worker Thread]
  MultiSocketRUDPCore::OnRecvPacket(threadId)
    │
    ├─ recvIOCompletedContexts[threadId].Dequeue(&context)
    ├─ session->nowInProcessingRecvPacket = true
    ├─ session->recvBufferList.Dequeue(&buffer)
    │
    └─ packetProcessor->OnRecvPacket(
           *context->session,
           *buffer,
           span(context->clientAddrBuffer, sizeof(sockaddr_in)))
              │
              └─ ProcessByPacketType(session, clientAddr, buffer)
```

`RUDPPacketProcessor`는 `MultiSocketRUDPCore`가 `unique_ptr`로 소유한다. `OnRecvPacket()`은 검증과 분기만 수행하며, `NetBuffer` 해제는 호출자인 `MultiSocketRUDPCore::OnRecvPacket()`에서 수행한다.

---

## 2. OnRecvPacket — 사전 유효성 검사

```cpp
void RUDPPacketProcessor::OnRecvPacket(
    RUDPSession& session,
    NetBuffer& buffer,
    std::span<const unsigned char> clientAddrBuffer)
{
    // ① 헤더 최소 크기 확인
    if (buffer.GetUseSize() < df_HEADER_SIZE) {
        return;
    }

    // ② 헤더 기반 페이로드 길이 일치 확인
    if (buffer.GetUseSize() != GetPayloadLength(buffer)) {
        return;
    }

    // ③ 클라이언트 주소 버퍼 최소 크기 확인
    if (clientAddrBuffer.size() < sizeof(sockaddr_in)) {
        return;
    }

    // ④ 클라이언트 주소 복사
    sockaddr_in clientAddr{};
    std::ignore = memcpy_s(
        &clientAddr, sizeof(clientAddr),
        clientAddrBuffer.data(), sizeof(clientAddr));

    // ⑤ 타입별 처리 (ProcessByPacketType 내부에서 PacketType 추출)
    ProcessByPacketType(session, clientAddr, buffer);

}
```

**`GetPayloadLength` 구현:**

```cpp
static WORD GetPayloadLength(const NetBuffer& buffer)
{
    // buffer[0] = HeaderCode (1B)
    // buffer[1..2] = PayloadLen (2B, uint16_t LE)
    WORD payloadLen;
    memcpy(&payloadLen,
           &buffer.m_pSerializeBuffer[1],
           sizeof(WORD));
    return payloadLen + df_HEADER_SIZE;
    // PayloadLen = 헤더 이후 바이트 수
    // GetUseSize = 전체 바이트 수 (헤더 포함)
    // → payloadLen + HEADER_SIZE == GetUseSize 이어야 함
}
```

---

## 3. ProcessByPacketType — 타입별 분기 전체

```cpp
void RUDPPacketProcessor::ProcessByPacketType(
    RUDPSession& session,
    NetBuffer& recvPacket,
    const sockaddr_in& clientAddr,
    BYTE packetTypeByte)
{
    // 세션 암호화 컨텍스트 획득
    const auto* sessionSalt      = sessionDelegate.GetSessionSalt(session);
    const auto& sessionKeyHandle = sessionDelegate.GetSessionKeyHandle(session);

    switch (static_cast<PACKET_TYPE>(packetTypeByte)) {

    // ────────────────────────────────────────────────────────
    case PACKET_TYPE::CONNECT_TYPE:
    {
        // sessionSalt가 null이면 InitReserveSession이 완료 안 됨
        if (sessionSalt == nullptr) {
            LOG_ERROR("CONNECT_TYPE received but session not initialized");
            break;
        }

        constexpr bool isCorePacket = true;
        auto dir = PACKET_DIRECTION::CLIENT_TO_SERVER;
        DECODE_PACKET()  // 실패 시 break

        if (!session.TryConnect(recvPacket, clientAddr)) {
            LOG_ERROR(std::format("TryConnect failed. SessionId={}",
                session.GetSessionId()));
            break;
        }

        sessionManager.IncrementConnectedCount();
        LOG_DEBUG(std::format("Client connected. SessionId={}",
            session.GetSessionId()));
        break;
    }

    // ────────────────────────────────────────────────────────
    case PACKET_TYPE::DISCONNECT_TYPE:
    {
        if (!session.CanProcessPacket(clientAddr)) break;

        constexpr bool isCorePacket = true;
        auto dir = PACKET_DIRECTION::CLIENT_TO_SERVER;
        DECODE_PACKET()

        session.DoDisconnect(DISCONNECT_REASON::NORMAL);
        LOG_DEBUG(std::format("Client disconnected (DISCONNECT_TYPE). SessionId={}",
            session.GetSessionId()));
        break;
    }

    // ────────────────────────────────────────────────────────
    case PACKET_TYPE::SEND_TYPE:
    {
        if (!session.CanProcessPacket(clientAddr)) break;
        if (session.IsReleasing()) break;

        constexpr bool isCorePacket = false;  // PacketId 포함
        auto dir = PACKET_DIRECTION::CLIENT_TO_SERVER;
        DECODE_PACKET()

        if (!session.OnRecvPacket(recvPacket)) {
            // 순서 보장 큐 가득 참 또는 ProcessPacket 실패
            session.DoDisconnect(DISCONNECT_REASON::BY_ERROR);
            break;
        }

        // TPS 카운터
        tps.fetch_add(1, std::memory_order_relaxed);
        break;
    }

    // ────────────────────────────────────────────────────────
    case PACKET_TYPE::SEND_REPLY_TYPE:
    case PACKET_TYPE::HEARTBEAT_REPLY_TYPE:
    {
        if (!session.CanProcessPacket(clientAddr)) break;

        constexpr bool isCorePacket = true;
        auto dir = PACKET_DIRECTION::CLIENT_TO_SERVER_REPLY;
        DECODE_PACKET()

        session.OnSendReply(recvPacket);
        break;
    }

    // ────────────────────────────────────────────────────────
    default:
        LOG_ERROR(std::format("Unknown packet type: {}", packetTypeByte));
        break;
    }
}
```

---

## 4. DECODE_PACKET 매크로

```cpp
#define DECODE_PACKET() \
    if (!PacketCryptoHelper::DecodePacket( \
            recvPacket, \
            sessionSalt, \
            SESSION_SALT_SIZE, \
            sessionKeyHandle, \
            isCorePacket, \
            dir)) \
    { \
        LOG_ERROR(std::format( \
            "DecodePacket failed. type={}, sessionId={}", \
            static_cast<int>(packetTypeByte), session.GetSessionId())); \
        break; \
    }
```

**`break`로 조용히 폐기하는 이유:**

AES-GCM 인증 실패의 원인은 알 수 없다:
- 위변조 패킷 (중간자 공격 시도)
- 네트워크 오류로 데이터 깨짐
- 클라이언트 버그 (잘못된 Nonce 생성)

어떤 경우든 해당 패킷 1개만 폐기하는 것이 안전하다.  
`DoDisconnect(reason)`를 즉시 호출하면 정상 클라이언트가 네트워크 노이즈 하나로  
연결이 끊길 수 있다. 반복적인 실패는 로그로 파악한다.

---

## 5. CanProcessPacket — 스푸핑 방지

```cpp
// RUDPSession::CanProcessPacket
bool CanProcessPacket(const sockaddr_in& targetClientAddr) const
{
    return CheckMyClient(targetClientAddr) && !IsReleasing();
}

bool CheckMyClient(const sockaddr_in& target) const
{
    // IP 주소 비교
    return clientAddr.sin_addr.S_un.S_addr == target.sin_addr.S_un.S_addr
        // 포트 비교 (network byte order)
        && clientAddr.sin_port == target.sin_port;
}
```

**왜 이 검사가 필요한가:**

```
세션 발급 후:
  세션 A → Port 50001 → clientAddr = 192.168.1.10:12345

다른 IP에서 Port 50001로 패킷 전송:
  송신자: 192.168.1.99:54321
  → CheckMyClient 실패 → break (폐기)

다른 세션의 IP에서 시퀀스 번호를 맞춰 공격:
  → CheckMyClient 실패 → break
  → 설령 IP를 맞추더라도 AES-GCM 인증 실패 (다른 세션 키)
```

**이중 방어:**
1. IP+Port 검증 (빠른 1차 필터)
2. AES-GCM 인증 태그 검증 (2차, 암호학적 보장)

---

## 6. TPS 카운터

```cpp
std::atomic<int32_t> tps{ 0 };

// SEND_TYPE 처리 성공마다
tps.fetch_add(1, std::memory_order_relaxed);
// ↑ relaxed: 순서 보장 불필요, 최대 성능

// 외부 조회
int32_t RUDPPacketProcessor::GetTPS() const {
    return tps.load(std::memory_order_relaxed);
}

// 초기화
void RUDPPacketProcessor::ResetTPS() {
    tps.store(0, std::memory_order_relaxed);
}
```

**`MultiSocketRUDPCore`에서의 위임:**

```cpp
int32_t MultiSocketRUDPCore::GetTPS() const {
    return packetProcessor->GetTPS();
}

void MultiSocketRUDPCore::ResetTPS() const {
    packetProcessor->ResetTPS();
}
```

**TPS 측정 패턴:**

```cpp
// 모니터링 스레드 또는 Ticker에서
std::thread monitor([&]() {
    while (!core.IsServerStopped()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        int32_t tps = core.GetTPS();
        core.ResetTPS();
        uint16_t sessions = core.GetNowSessionCount();

        LOG_DEBUG(std::format("TPS={}, Sessions={}", tps, sessions));
    }
});
```

---

## 7. 의존성 및 생성자

```cpp
class RUDPPacketProcessor {
    RUDPSessionManager& sessionManager;
    ISessionDelegate& sessionDelegate;

    std::atomic<int32_t> tps{ 0 };
public:
    RUDPPacketProcessor(
        RUDPSessionManager& inSessionManager,
        ISessionDelegate& inSessionDelegate)
        : sessionManager(inSessionManager)
        , sessionDelegate(inSessionDelegate)
    {}
};
```

암호화는 `PacketCryptoHelper` 정적 경로를 통해 처리하며, 현재 생성자에서 crypto helper를 주입하지 않는다.

---

## 관련 문서
- [[PacketProcessing]] — ProcessByPacketType의 각 분기 상세
- [[RUDPSession]] — TryConnect, OnRecvPacket, OnSendReply, DoDisconnect 구현
- [[PacketCryptoHelper]] — DECODE_PACKET 매크로에서 사용
- [[ThreadModel]] — RecvLogic Worker Thread 상세
