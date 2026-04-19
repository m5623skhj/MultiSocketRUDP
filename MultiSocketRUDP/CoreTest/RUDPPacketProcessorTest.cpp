#include "PreCompile.h"
#include <gtest/gtest.h>

#include "RUDPPacketProcessor.h"
#include "RUDPSessionManager.h"
#include "MultiSocketRUDPCore.h"
#include "MockSessionDelegate.h"
#include "NetServerSerializeBuffer.h"

// ============================================================
// 테스트용 최소 RUDPSession 서브클래스
//   - RUDPSession 생성자가 protected 이므로 파생 클래스가 필요
// ============================================================
class PacketProcessorTestSession final : public RUDPSession
{
public:
    explicit PacketProcessorTestSession(MultiSocketRUDPCore& inCore)
        : RUDPSession(inCore) {
    }
};

// ============================================================
// RUDPPacketProcessor 단위 테스트 픽스처
//
// ┌─── 테스트 가능 범위 ────────────────────────────────────────────────┐
// │                                                                     │
// │  OnRecvPacket 의 두 early-return 조건                               │
// │    ① GetUseSize() != GetPayloadLength()                             │
// │    ② clientAddrBuffer.size() < sizeof(sockaddr_in)                  │
// │                                                                     │
// │  ProcessByPacketType 의 null sessionKeyHandle early-return          │
// │                                                                     │
// │  ProcessByPacketType 의 switch 분기 (fake non-null keyHandle 사용)  │
// │    ─ CONNECT_TYPE     : DECODE_PACKET 실패 → TryConnect 미호출      │
// │    ─ SEND_TYPE        : CanProcessPacket false → break              │
// │    ─ SEND_TYPE        : CanProcessPacket true → DECODE_PACKET 실패  │
// │    ─ DISCONNECT_TYPE  : CanProcessPacket false/true + DECODE 실패   │
// │    ─ SEND_REPLY_TYPE  : CanProcessPacket false/true + DECODE 실패   │
// │    ─ HEARTBEAT_REPLY  : CanProcessPacket false/true + DECODE 실패   │
// │    ─ default (invalid): LOG_ERROR 후 no-crash                       │
// │                                                                     │
// │  GetTPS / ResetTPS 초기값 및 멱등성                                  │
// │                                                                     │
// │ [불가] TPS 증가 검증                                                 │
// │        SEND_TYPE 성공까지 가려면 유효한 AES-GCM 키가 필요            │
// │        → 통합 테스트 영역                                            │
// │ [불가] CONNECT_TYPE / DISCONNECT_TYPE 완전 성공 경로                 │
// │        → crypto 초기화 + 세션 상태 협력 필요 → 통합 테스트 영역     │
// └─────────────────────────────────────────────────────────────────────┘
//
// ┌─── fake non-null keyHandle 안전성 근거 ─────────────────────────────┐
// │                                                                     │
// │  mockDelegate.dummyKeyHandle = reinterpret_cast<BCRYPT_KEY_HANDLE>(1)│
// │  으로 설정하면 ProcessByPacketType 의 null 검사를 통과해 switch 에   │
// │  진입할 수 있다.                                                     │
// │                                                                     │
// │  DECODE_PACKET 매크로 내부에서 PacketCryptoHelper::DecodePacket 가  │
// │  호출되는데, 이 함수는 BCryptDecrypt 호출 전에                       │
// │  minimumSize 체크를 먼저 수행한다:                                   │
// │    minimumCorePacketSize = sizeof(PacketSequence:8)                 │
// │                          + AUTH_TAG_SIZE:16 = 24                    │
// │    minimumPacketSize     = sizeof(PacketSequence:8)                 │
// │                          + sizeof(PacketId:4)                       │
// │                          + AUTH_TAG_SIZE:16 = 28                    │
// │                                                                     │
// │  1-byte 버퍼에서 packetType(1B) 을 읽으면 GetUseSize() = 0 이 되어  │
// │  0 < 24 (또는 28) 조건에서 false 반환 → BCryptDecrypt 미호출.       │
// │  따라서 fake handle 을 가진 BCrypt API 에 대한 UB/crash 우려가 없다. │
// └─────────────────────────────────────────────────────────────────────┘
class RUDPPacketProcessorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        processor = std::make_unique<RUDPPacketProcessor>(sessionManager, mockDelegate);
    }

    void TearDown() override
    {
        processor.reset();
    }

    // ── 의존성 ──────────────────────────────────────────────
    MockSessionDelegate mockDelegate;

    // RUDPSessionManager: 생성자만 호출 (Initialize 미호출)
    // CONNECT_TYPE 성공 시 IncrementConnectedCount() 만 사용하며 초기화 불필요
    MultiSocketRUDPCore coreStub{ L"", L"" };
    RUDPSessionManager  sessionManager{ 0, coreStub, mockDelegate };

    std::unique_ptr<RUDPPacketProcessor> processor;

    MultiSocketRUDPCore sessionCoreStub{ L"", L"" };
    PacketProcessorTestSession session{ sessionCoreStub };

    // ── 버퍼 헬퍼 ───────────────────────────────────────────
    //
    // OnRecvPacket 의 size 체크:
    //   if (buffer.GetUseSize() != GetPayloadLength(buffer)) return;
    //   GetUseSize()        = m_iWrite - m_iRead
    //   GetPayloadLength()  = *(WORD*)(&buf[m_iRead + 1])
    //
    // ProcessByPacketType 의 type 읽기:
    //   recvPacket >> packetType;  // reads 1 byte from m_iRead, advances m_iRead
    //
    // 모든 헬퍼는 m_iRead / m_iWrite 를 명시적으로 설정해
    // NetBuffer::Alloc() 의 초기값에 의존하지 않는다.

    // ① size 체크를 통과시켜 ProcessByPacketType 까지 진입시키는 빈 버퍼
    //    GetUseSize() == GetPayloadLength() == 0
    //    ProcessByPacketType 에서 type 을 읽으면 GetUseSize() = -1 이 되어
    //    DECODE_PACKET 의 minimumSize 체크에서 false 반환 (BCrypt 미호출)
    static NetBuffer* MakePassthroughBuffer()
    {
        NetBuffer* buf = NetBuffer::Alloc();
        buf->m_iRead = 0;
        buf->m_iWrite = 0;
        buf->m_pSerializeBuffer[1] = 0;  // GetPayloadLength low byte
        buf->m_pSerializeBuffer[2] = 0;  // GetPayloadLength high byte → 0
        // GetUseSize() = 0, GetPayloadLength() = 0  →  size 체크 통과
        return buf;
    }

    // ② size 불일치 버퍼 — OnRecvPacket 첫 번째 guard 에서 반환
    //    GetUseSize() = 1, GetPayloadLength() = 0  →  1 != 0
    static NetBuffer* MakeMismatchedSizeBuffer()
    {
        NetBuffer* buf = NetBuffer::Alloc();
        buf->m_iRead = 0;
        buf->m_iWrite = 1;               // GetUseSize() = 1
        buf->m_pSerializeBuffer[1] = 0;  // GetPayloadLength() = 0
        buf->m_pSerializeBuffer[2] = 0;
        // 1 != 0 → size 체크에서 즉시 반환
        return buf;
    }

    // ③ 패킷 타입 라우팅 테스트 전용 1-byte 버퍼
    //    buf[0]     = packetType 바이트
    //    buf[1:2]   = payloadLength = 1  →  GetUseSize() = 1 == GetPayloadLength() = 1
    //    ProcessByPacketType 가 packetType 읽은 후 GetUseSize() = 0
    //    → DECODE_PACKET 의 minimumSize 체크(0 < 24 or 28)에서 false 반환
    //    → BCryptDecrypt 는 절대 호출되지 않는다
    static NetBuffer* MakeSingleBytePacketBuffer(const PACKET_TYPE packetType)
    {
        NetBuffer* buf = NetBuffer::Alloc();
        buf->m_iRead = 0;
        buf->m_iWrite = 0;

        const auto typeVal = static_cast<BYTE>(packetType);
        *buf << typeVal;  // buf[0] = typeVal, m_iWrite = 1

        // GetPayloadLength reads *(WORD*)(&buf[m_iRead + 1]) = *(WORD*)(&buf[1])
        buf->m_pSerializeBuffer[1] = 1;  // payloadLength = 1 (low byte)
        buf->m_pSerializeBuffer[2] = 0;  // payloadLength high byte
        // GetUseSize() = 1, GetPayloadLength() = 1  →  size 체크 통과
        return buf;
    }

    // ── addrBuffer 헬퍼 ─────────────────────────────────────

    static std::vector<unsigned char> MakeValidAddrBuffer()
    {
        return std::vector<unsigned char>(sizeof(sockaddr_in), 0);
    }

    static std::vector<unsigned char> MakeTooSmallAddrBuffer()
    {
        return std::vector<unsigned char>(sizeof(sockaddr_in) - 1, 0);
    }

    // ── 픽스처 헬퍼 ─────────────────────────────────────────

    // ProcessByPacketType 의 null keyHandle 검사를 통과시킨다.
    // dummyKeyHandle = reinterpret_cast<BCRYPT_KEY_HANDLE>(1) — non-null 포인터
    // BCrypt 는 minimumSize 체크 이전에 호출되지 않으므로 crash 없음 (상단 설명 참조)
    void SetupFakeKeyHandle()
    {
        mockDelegate.dummyKeyHandle = reinterpret_cast<BCRYPT_KEY_HANDLE>(1);
    }
};

// ============================================================
// 1. 프로세서 생성 / 소멸 안전성
// ============================================================

TEST_F(RUDPPacketProcessorTest, Processor_CreateAndDestroy_NoCrash)
{
    MockSessionDelegate localDelegate;
    MultiSocketRUDPCore localCore{ L"", L"" };
    RUDPSessionManager  localManager{ 0, localCore, localDelegate };

    EXPECT_NO_THROW({
        RUDPPacketProcessor localProcessor(localManager, localDelegate);
        EXPECT_EQ(localProcessor.GetTPS(), 0);
        });
}

// ============================================================
// 2. GetTPS / ResetTPS — 초기값 및 멱등성
// ============================================================

TEST_F(RUDPPacketProcessorTest, GetTPS_Initial_ReturnsZero)
{
    EXPECT_EQ(processor->GetTPS(), 0);
}

TEST_F(RUDPPacketProcessorTest, ResetTPS_OnInitial_StaysZero)
{
    processor->ResetTPS();
    EXPECT_EQ(processor->GetTPS(), 0);
}

TEST_F(RUDPPacketProcessorTest, ResetTPS_CalledMultipleTimes_StaysZero)
{
    for (int i = 0; i < 5; ++i)
    {
        processor->ResetTPS();
    }
    EXPECT_EQ(processor->GetTPS(), 0);
}

TEST_F(RUDPPacketProcessorTest, GetTPS_AfterPacketCallsAndReset_ReturnsZero)
{
    // 패킷 처리 시도(TPS 는 증가 안함) 후 ResetTPS 가 항상 0 으로 돌아오는지 확인
    const auto validAddr = MakeValidAddrBuffer();
    NetBuffer* buf = MakePassthroughBuffer();
    processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr));
    NetBuffer::Free(buf);

    processor->ResetTPS();
    EXPECT_EQ(processor->GetTPS(), 0);
}

// ============================================================
// 3. OnRecvPacket — 첫 번째 guard: size 불일치
//    GetUseSize() != GetPayloadLength()  →  즉시 반환
// ============================================================

TEST_F(RUDPPacketProcessorTest, OnRecvPacket_SizeMismatch_TpsNotIncremented)
{
    NetBuffer* buf = MakeMismatchedSizeBuffer();
    const auto validAddr = MakeValidAddrBuffer();

    processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr));

    EXPECT_EQ(processor->GetTPS(), 0);
    NetBuffer::Free(buf);
}

TEST_F(RUDPPacketProcessorTest, OnRecvPacket_SizeMismatch_NoDelegateMethodsCalled)
{
    NetBuffer* buf = MakeMismatchedSizeBuffer();
    const auto validAddr = MakeValidAddrBuffer();

    processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr));

    // ProcessByPacketType 미진입 → 어떤 delegate 메서드도 호출되지 않아야 함
    EXPECT_EQ(mockDelegate.onRecvPacketCount, 0);
    EXPECT_EQ(mockDelegate.tryConnectCount, 0);
    EXPECT_EQ(mockDelegate.onSendReplyCount, 0);
    EXPECT_EQ(mockDelegate.disconnectCount, 0);
    NetBuffer::Free(buf);
}

TEST_F(RUDPPacketProcessorTest, OnRecvPacket_SizeMismatch_NoCrash)
{
    NetBuffer* buf = MakeMismatchedSizeBuffer();
    const auto validAddr = MakeValidAddrBuffer();

    EXPECT_NO_THROW(
        processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr)));

    NetBuffer::Free(buf);
}

// ============================================================
// 4. OnRecvPacket — 두 번째 guard: addrBuffer 크기 부족
//    clientAddrBuffer.size() < sizeof(sockaddr_in)  →  반환
//    ※ size 체크는 이미 통과한 상태(MakePassthroughBuffer 사용)
// ============================================================

TEST_F(RUDPPacketProcessorTest, OnRecvPacket_TooSmallAddrBuffer_TpsNotIncremented)
{
    NetBuffer* buf = MakePassthroughBuffer();  // size 체크 확실히 통과
    const auto smallAddr = MakeTooSmallAddrBuffer();

    processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(smallAddr));

    EXPECT_EQ(processor->GetTPS(), 0);
    NetBuffer::Free(buf);
}

TEST_F(RUDPPacketProcessorTest, OnRecvPacket_TooSmallAddrBuffer_NoDelegateMethodsCalled)
{
    NetBuffer* buf = MakePassthroughBuffer();
    const auto smallAddr = MakeTooSmallAddrBuffer();

    processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(smallAddr));

    EXPECT_EQ(mockDelegate.onRecvPacketCount, 0);
    EXPECT_EQ(mockDelegate.tryConnectCount, 0);
    NetBuffer::Free(buf);
}

TEST_F(RUDPPacketProcessorTest, OnRecvPacket_EmptyAddrBuffer_EarlyReturn)
{
    NetBuffer* buf = MakePassthroughBuffer();
    const std::vector<unsigned char> emptyAddr{};

    processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(emptyAddr));

    EXPECT_EQ(processor->GetTPS(), 0);
    EXPECT_EQ(mockDelegate.onRecvPacketCount, 0);
    NetBuffer::Free(buf);
}

// ============================================================
// 5. OnRecvPacket — addrBuffer 경계값
//    sizeof(sockaddr_in) - 1 : 실패
//    sizeof(sockaddr_in)     : 통과 (ProcessByPacketType 진입, null keyHandle 에서 반환)
// ============================================================

TEST_F(RUDPPacketProcessorTest, OnRecvPacket_AddrBuffer_OneLessThanRequired_EarlyReturn)
{
    NetBuffer* buf = MakePassthroughBuffer();
    const std::vector<unsigned char> oneLess(sizeof(sockaddr_in) - 1, 0);

    processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(oneLess));

    EXPECT_EQ(mockDelegate.onRecvPacketCount, 0);
    EXPECT_EQ(mockDelegate.tryConnectCount, 0);
    NetBuffer::Free(buf);
}

TEST_F(RUDPPacketProcessorTest, OnRecvPacket_AddrBuffer_ExactRequiredSize_PassesSizeCheck_NoCrash)
{
    // 정확히 sizeof(sockaddr_in) 이면 두 번째 guard 를 통과해 ProcessByPacketType 진입
    // → null keyHandle 검사에서 LOG_ERROR 후 반환 (no crash)
    NetBuffer* buf = MakePassthroughBuffer();
    const auto exactAddr = MakeValidAddrBuffer();

    EXPECT_NO_THROW(
        processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(exactAddr)));

    NetBuffer::Free(buf);
}

// ============================================================
// 6. OnRecvPacket — null sessionKeyHandle early-return
//    두 guard 를 통과하지만 ProcessByPacketType 첫 줄에서 반환
//    dummyKeyHandle = nullptr (MockSessionDelegate 기본값)
// ============================================================

TEST_F(RUDPPacketProcessorTest, OnRecvPacket_NullKeyHandle_NoCrash)
{
    NetBuffer* buf = MakePassthroughBuffer();
    const auto validAddr = MakeValidAddrBuffer();

    EXPECT_NO_THROW(
        processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr)));

    NetBuffer::Free(buf);
}

TEST_F(RUDPPacketProcessorTest, OnRecvPacket_NullKeyHandle_TpsNotIncremented)
{
    NetBuffer* buf = MakePassthroughBuffer();
    const auto validAddr = MakeValidAddrBuffer();

    processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr));

    EXPECT_EQ(processor->GetTPS(), 0);
    NetBuffer::Free(buf);
}

TEST_F(RUDPPacketProcessorTest, OnRecvPacket_NullKeyHandle_NoDelegateMethodsCalled)
{
    NetBuffer* buf = MakePassthroughBuffer();
    const auto validAddr = MakeValidAddrBuffer();

    processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr));

    EXPECT_EQ(mockDelegate.onRecvPacketCount, 0);
    EXPECT_EQ(mockDelegate.tryConnectCount, 0);
    EXPECT_EQ(mockDelegate.onSendReplyCount, 0);
    EXPECT_EQ(mockDelegate.disconnectCount, 0);
    NetBuffer::Free(buf);
}

// ============================================================
// 7. OnRecvPacket — 반복 호출 안전성
// ============================================================

TEST_F(RUDPPacketProcessorTest, OnRecvPacket_MultipleCalls_NoCrash)
{
    const auto validAddr = MakeValidAddrBuffer();

    for (int i = 0; i < 10; ++i)
    {
        NetBuffer* buf = MakePassthroughBuffer();
        EXPECT_NO_THROW(
            processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr)))
            << "Crashed on iteration " << i;
        NetBuffer::Free(buf);
    }

    EXPECT_EQ(processor->GetTPS(), 0);
}

TEST_F(RUDPPacketProcessorTest, OnRecvPacket_RepeatedSizeMismatch_NoCrash)
{
    const auto validAddr = MakeValidAddrBuffer();

    for (int i = 0; i < 5; ++i)
    {
        NetBuffer* buf = MakeMismatchedSizeBuffer();
        EXPECT_NO_THROW(
            processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr)))
            << "Crashed on iteration " << i;
        NetBuffer::Free(buf);
    }

    EXPECT_EQ(mockDelegate.onRecvPacketCount, 0);
}

// ============================================================
// 8. OnRecvPacket — 경로 전환 후 상태 오염 없음
//    size 불일치 → size 일치로 전환해도 이전 실패가 이후 경로에 영향 없음
// ============================================================

TEST_F(RUDPPacketProcessorTest, OnRecvPacket_MismatchThenPassthrough_NoCrossContamination)
{
    const auto validAddr = MakeValidAddrBuffer();

    // 첫 번째: size 불일치 경로
    {
        NetBuffer* buf = MakeMismatchedSizeBuffer();
        processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr));
        NetBuffer::Free(buf);
    }
    EXPECT_EQ(processor->GetTPS(), 0);
    EXPECT_EQ(mockDelegate.onRecvPacketCount, 0);

    // 두 번째: size 일치 → ProcessByPacketType 진입 → null keyHandle 에서 반환
    {
        NetBuffer* buf = MakePassthroughBuffer();
        EXPECT_NO_THROW(
            processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr)));
        NetBuffer::Free(buf);
    }
    EXPECT_EQ(processor->GetTPS(), 0);
    EXPECT_EQ(mockDelegate.onRecvPacketCount, 0);
}

// ============================================================
// 9. ProcessByPacketType — CONNECT_TYPE
//    CanProcessPacket 체크 없음, DECODE_PACKET 실패(minimumSize) → break
//    → TryConnect 미호출
// ============================================================

TEST_F(RUDPPacketProcessorTest, ProcessByPacketType_ConnectType_DecryptFails_TryConnectNotCalled)
{
    SetupFakeKeyHandle();
    NetBuffer* buf = MakeSingleBytePacketBuffer(PACKET_TYPE::CONNECT_TYPE);
    const auto validAddr = MakeValidAddrBuffer();

    processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr));

    EXPECT_EQ(mockDelegate.tryConnectCount, 0);
    EXPECT_EQ(processor->GetTPS(), 0);
    NetBuffer::Free(buf);
}

TEST_F(RUDPPacketProcessorTest, ProcessByPacketType_ConnectType_NoCrash)
{
    SetupFakeKeyHandle();
    NetBuffer* buf = MakeSingleBytePacketBuffer(PACKET_TYPE::CONNECT_TYPE);
    const auto validAddr = MakeValidAddrBuffer();

    EXPECT_NO_THROW(
        processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr)));

    NetBuffer::Free(buf);
}

// ============================================================
// 10. ProcessByPacketType — SEND_TYPE
//     ① CanProcessPacket = false → 즉시 break (DECODE_PACKET 미호출)
//     ② CanProcessPacket = true  → DECODE_PACKET 실패 → break
//        OnRecvPacket/TPS 모두 미호출
// ============================================================

TEST_F(RUDPPacketProcessorTest, ProcessByPacketType_SendType_CanProcessPacketFalse_OnRecvNotCalled)
{
    SetupFakeKeyHandle();
    mockDelegate.canProcessReturn = false;
    NetBuffer* buf = MakeSingleBytePacketBuffer(PACKET_TYPE::SEND_TYPE);
    const auto validAddr = MakeValidAddrBuffer();

    processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr));

    EXPECT_EQ(mockDelegate.onRecvPacketCount, 0);
    EXPECT_EQ(processor->GetTPS(), 0);
    NetBuffer::Free(buf);
}

TEST_F(RUDPPacketProcessorTest, ProcessByPacketType_SendType_CanProcessPacketTrue_DecryptFails_OnRecvNotCalled)
{
    SetupFakeKeyHandle();
    mockDelegate.canProcessReturn = true;
    NetBuffer* buf = MakeSingleBytePacketBuffer(PACKET_TYPE::SEND_TYPE);
    const auto validAddr = MakeValidAddrBuffer();

    processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr));

    // DECODE_PACKET 실패 → break → OnRecvPacket delegate 미호출
    EXPECT_EQ(mockDelegate.onRecvPacketCount, 0);
    EXPECT_EQ(processor->GetTPS(), 0);
    NetBuffer::Free(buf);
}

// ============================================================
// 11. ProcessByPacketType — DISCONNECT_TYPE
//     ① CanProcessPacket = false → break → Disconnect 미호출
//     ② CanProcessPacket = true  → DECODE_PACKET 실패 → break → Disconnect 미호출
// ============================================================

TEST_F(RUDPPacketProcessorTest, ProcessByPacketType_DisconnectType_CanProcessPacketFalse_DisconnectNotCalled)
{
    SetupFakeKeyHandle();
    mockDelegate.canProcessReturn = false;
    NetBuffer* buf = MakeSingleBytePacketBuffer(PACKET_TYPE::DISCONNECT_TYPE);
    const auto validAddr = MakeValidAddrBuffer();

    processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr));

    EXPECT_EQ(mockDelegate.disconnectCount, 0);
    NetBuffer::Free(buf);
}

TEST_F(RUDPPacketProcessorTest, ProcessByPacketType_DisconnectType_CanProcessPacketTrue_DecryptFails_DisconnectNotCalled)
{
    SetupFakeKeyHandle();
    mockDelegate.canProcessReturn = true;
    NetBuffer* buf = MakeSingleBytePacketBuffer(PACKET_TYPE::DISCONNECT_TYPE);
    const auto validAddr = MakeValidAddrBuffer();

    processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr));

    EXPECT_EQ(mockDelegate.disconnectCount, 0);
    NetBuffer::Free(buf);
}

// ============================================================
// 12. ProcessByPacketType — SEND_REPLY_TYPE
//     ① CanProcessPacket = false → break → OnSendReply 미호출
//     ② CanProcessPacket = true  → DECODE_PACKET 실패 → break → OnSendReply 미호출
// ============================================================

TEST_F(RUDPPacketProcessorTest, ProcessByPacketType_SendReplyType_CanProcessPacketFalse_OnSendReplyNotCalled)
{
    SetupFakeKeyHandle();
    mockDelegate.canProcessReturn = false;
    NetBuffer* buf = MakeSingleBytePacketBuffer(PACKET_TYPE::SEND_REPLY_TYPE);
    const auto validAddr = MakeValidAddrBuffer();

    processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr));

    EXPECT_EQ(mockDelegate.onSendReplyCount, 0);
    NetBuffer::Free(buf);
}

TEST_F(RUDPPacketProcessorTest, ProcessByPacketType_SendReplyType_CanProcessPacketTrue_DecryptFails_OnSendReplyNotCalled)
{
    SetupFakeKeyHandle();
    mockDelegate.canProcessReturn = true;
    NetBuffer* buf = MakeSingleBytePacketBuffer(PACKET_TYPE::SEND_REPLY_TYPE);
    const auto validAddr = MakeValidAddrBuffer();

    processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr));

    EXPECT_EQ(mockDelegate.onSendReplyCount, 0);
    NetBuffer::Free(buf);
}

// ============================================================
// 13. ProcessByPacketType — HEARTBEAT_REPLY_TYPE
//     SEND_REPLY_TYPE 과 동일 핸들러를 공유하지만 별도 타입이므로 독립 검증
//     ① CanProcessPacket = false → break
//     ② CanProcessPacket = true  → DECODE_PACKET 실패 → break
// ============================================================

TEST_F(RUDPPacketProcessorTest, ProcessByPacketType_HeartbeatReplyType_CanProcessPacketFalse_OnSendReplyNotCalled)
{
    SetupFakeKeyHandle();
    mockDelegate.canProcessReturn = false;
    NetBuffer* buf = MakeSingleBytePacketBuffer(PACKET_TYPE::HEARTBEAT_REPLY_TYPE);
    const auto validAddr = MakeValidAddrBuffer();

    processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr));

    EXPECT_EQ(mockDelegate.onSendReplyCount, 0);
    NetBuffer::Free(buf);
}

TEST_F(RUDPPacketProcessorTest, ProcessByPacketType_HeartbeatReplyType_CanProcessPacketTrue_DecryptFails_OnSendReplyNotCalled)
{
    SetupFakeKeyHandle();
    mockDelegate.canProcessReturn = true;
    NetBuffer* buf = MakeSingleBytePacketBuffer(PACKET_TYPE::HEARTBEAT_REPLY_TYPE);
    const auto validAddr = MakeValidAddrBuffer();

    processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr));

    EXPECT_EQ(mockDelegate.onSendReplyCount, 0);
    NetBuffer::Free(buf);
}

// ============================================================
// 14. ProcessByPacketType — default 케이스 (알 수 없는 타입)
//     LOG_ERROR 를 출력하고 break — no crash
// ============================================================

// 0xFF: 정의되지 않은 PACKET_TYPE 값 → default 케이스
TEST_F(RUDPPacketProcessorTest, ProcessByPacketType_UnknownType_DefaultCase_NoCrash)
{
    SetupFakeKeyHandle();
    NetBuffer* buf = MakeSingleBytePacketBuffer(static_cast<PACKET_TYPE>(0xFF));
    const auto validAddr = MakeValidAddrBuffer();

    EXPECT_NO_THROW(
        processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr)));

    // default 케이스는 어떤 delegate 메서드도 호출하지 않는다
    EXPECT_EQ(mockDelegate.tryConnectCount, 0);
    EXPECT_EQ(mockDelegate.onRecvPacketCount, 0);
    EXPECT_EQ(mockDelegate.onSendReplyCount, 0);
    EXPECT_EQ(mockDelegate.disconnectCount, 0);
    EXPECT_EQ(processor->GetTPS(), 0);
    NetBuffer::Free(buf);
}

// INVALID_TYPE(= 0): 프로토콜에 존재하지만 switch 에 처리 없음 → default
TEST_F(RUDPPacketProcessorTest, ProcessByPacketType_InvalidType_DefaultCase_NoCrash)
{
    SetupFakeKeyHandle();
    NetBuffer* buf = MakeSingleBytePacketBuffer(PACKET_TYPE::INVALID_TYPE);
    const auto validAddr = MakeValidAddrBuffer();

    EXPECT_NO_THROW(
        processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr)));

    EXPECT_EQ(processor->GetTPS(), 0);
    NetBuffer::Free(buf);
}

// HEARTBEAT_TYPE: 서버→클라이언트 방향 타입이므로 클라이언트→서버 수신 시 default
TEST_F(RUDPPacketProcessorTest, ProcessByPacketType_HeartbeatType_UnhandledOnServer_DefaultCase_NoCrash)
{
    SetupFakeKeyHandle();
    NetBuffer* buf = MakeSingleBytePacketBuffer(PACKET_TYPE::HEARTBEAT_TYPE);
    const auto validAddr = MakeValidAddrBuffer();

    EXPECT_NO_THROW(
        processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr)));

    EXPECT_EQ(processor->GetTPS(), 0);
    NetBuffer::Free(buf);
}

// ============================================================
// 15. 종합: fake keyHandle 경로에서 어떤 타입을 받아도
//     TPS 가 증가하지 않고 크래시 없음
// ============================================================

TEST_F(RUDPPacketProcessorTest, ProcessByPacketType_AllTypes_NoTpsIncrement)
{
    SetupFakeKeyHandle();
    const auto validAddr = MakeValidAddrBuffer();

    // CanProcessPacket=true 상태에서 각 타입 처리 시도
    mockDelegate.canProcessReturn = true;

    const std::vector<PACKET_TYPE> allTypes = {
        PACKET_TYPE::CONNECT_TYPE,
        PACKET_TYPE::DISCONNECT_TYPE,
        PACKET_TYPE::SEND_TYPE,
        PACKET_TYPE::SEND_REPLY_TYPE,
        PACKET_TYPE::HEARTBEAT_TYPE,
        PACKET_TYPE::HEARTBEAT_REPLY_TYPE,
        PACKET_TYPE::INVALID_TYPE,
        static_cast<PACKET_TYPE>(0xFF),
    };

    for (const auto packetType : allTypes)
    {
        NetBuffer* buf = MakeSingleBytePacketBuffer(packetType);
        EXPECT_NO_THROW(
            processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr)))
            << "Crashed for PACKET_TYPE = " << static_cast<int>(packetType);
        NetBuffer::Free(buf);
    }

    EXPECT_EQ(processor->GetTPS(), 0);
}