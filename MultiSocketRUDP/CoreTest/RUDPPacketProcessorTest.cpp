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
        : RUDPSession(inCore) {}
};

// ============================================================
// RUDPPacketProcessor 단위 테스트 픽스처
//
// 테스트 가능 범위와 제한:
//   [가능] OnRecvPacket 의 두 가지 early-return 조건
//          ① GetUseSize() != GetPayloadLength()
//          ② clientAddrBuffer.size() < sizeof(sockaddr_in)
//   [가능] ProcessByPacketType 의 null sessionKeyHandle early-return
//   [가능] GetTPS / ResetTPS 의 초기값 및 멱등성
//   [불가] TPS 증가 검증 (SEND_TYPE 패킷이 OnRecvPacket 까지 성공하려면
//          유효한 AES-GCM 키 핸들이 필요하므로 단위 테스트 범위 외)
//   [불가] CONNECT_TYPE 성공 경로 (crypto 초기화 + TryConnect 내부에서
//          RUDPSession 상태를 조작하는 복잡한 협력이 필요)
//
// 설계 주안점:
//   - RUDPSessionManager 는 생성자만 호출 (Initialize 미호출)
//     → CONNECT_TYPE 성공 시 IncrementConnectedCount() 만 사용하며 안전
//   - MockSessionDelegate 의 주요 기본값
//       dummyKeyHandle = nullptr → ProcessByPacketType 에서 null 검사 후 즉시 반환
//       dummySalt = unsigned char[16]{} → 포인터 자체는 유효 (null 아님)
//       canProcessReturn = true
//   - NetBuffer 는 Alloc/Free 로 직접 관리, 각 테스트에서 해제
//
// ┌─────────────────── ProcessByPacketType 실행 흐름 ───────────────────┐
// │ 1. recvPacket >> packetType  (1 byte 읽기)                          │
// │ 2. GetSessionKeyHandle / GetSessionSalt → null 검사 → null 이면 return │
// │ 3. switch(packetType) { ... }                                        │
// └─────────────────────────────────────────────────────────────────────┘
//
// dummyKeyHandle == nullptr 이므로 step 2 에서 항상 조기 반환.
// switch 분기(step 3)는 keyHandle 이 유효한 통합 테스트에서 검증.
// ============================================================
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

    MultiSocketRUDPCore coreStub{ L"", L"" };
    RUDPSessionManager  sessionManager{ 0, coreStub, mockDelegate };

    std::unique_ptr<RUDPPacketProcessor> processor;

    MultiSocketRUDPCore sessionCoreStub{ L"", L"" };
    PacketProcessorTestSession session{ sessionCoreStub };

    // ── 버퍼 헬퍼 ───────────────────────────────────────────
    //
    // GetPayloadLength(buf) 구현:
    //   return *(WORD*)(&buf.m_pSerializeBuffer[buf.m_iRead + 1]);
    //
    // GetUseSize() 구현:
    //   return m_iWrite - m_iRead;
    //
    // 모든 헬퍼는 m_iRead / m_iWrite 를 명시적으로 설정하여
    // NetBuffer::Alloc() 의 초기값에 의존하지 않도록 한다.

    // ① GetUseSize() == GetPayloadLength() == 0
    //   → OnRecvPacket 의 size 체크를 통과시켜 ProcessByPacketType 까지 진입시킴
    //   → ProcessByPacketType 내부에서 null keyHandle 검사 후 조기 반환
    static NetBuffer* MakePassthroughBuffer()
    {
        NetBuffer* buf = NetBuffer::Alloc();
        buf->m_iRead  = 0;
        buf->m_iWrite = 0;
        // GetPayloadLength reads *(WORD*)(&buf[0 + 1]) = *(WORD*)(&buf[1])
        buf->m_pSerializeBuffer[1] = 0;
        buf->m_pSerializeBuffer[2] = 0;
        // GetUseSize() = 0 - 0 = 0  →  0 == 0 : size 체크 통과
        return buf;
    }

    // ② GetUseSize() == 1, GetPayloadLength() == 0
    //   → OnRecvPacket 의 size 체크에서 반환 (1 != 0)
    static NetBuffer* MakeMismatchedSizeBuffer()
    {
        NetBuffer* buf = NetBuffer::Alloc();
        buf->m_iRead  = 0;
        buf->m_iWrite = 1;                  // GetUseSize() = 1
        // GetPayloadLength reads *(WORD*)(&buf[0 + 1]) = *(WORD*)(&buf[1])
        buf->m_pSerializeBuffer[1] = 0;     // GetPayloadLength() = 0
        buf->m_pSerializeBuffer[2] = 0;
        // 1 != 0 : size 체크에서 즉시 반환
        return buf;
    }

    // ── addrBuffer 헬퍼 ─────────────────────────────────────

    // sizeof(sockaddr_in) 크기의 정상 주소 버퍼
    static std::vector<unsigned char> MakeValidAddrBuffer()
    {
        return std::vector<unsigned char>(sizeof(sockaddr_in), 0);
    }

    // sizeof(sockaddr_in) - 1 크기의 작은 버퍼 → addrBuffer size 체크에서 반환
    static std::vector<unsigned char> MakeTooSmallAddrBuffer()
    {
        return std::vector<unsigned char>(sizeof(sockaddr_in) - 1, 0);
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
// 2. GetTPS — 초기값
// ============================================================

TEST_F(RUDPPacketProcessorTest, GetTPS_Initial_ReturnsZero)
{
    EXPECT_EQ(processor->GetTPS(), 0);
}

// ============================================================
// 3. ResetTPS — 멱등성
// ============================================================

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

TEST_F(RUDPPacketProcessorTest, GetTPS_AfterResetTPS_StillZero)
{
    // 패킷 처리 시도 후 ResetTPS 가 항상 0 으로 돌아오는지 확인
    const auto validAddr = MakeValidAddrBuffer();
    NetBuffer* buf = MakePassthroughBuffer();
    processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr));
    NetBuffer::Free(buf);

    processor->ResetTPS();
    EXPECT_EQ(processor->GetTPS(), 0);
}

// ============================================================
// 4. OnRecvPacket — size 불일치 (GetUseSize != GetPayloadLength)
//    첫 번째 guard 에서 반환 → ProcessByPacketType 미진입
// ============================================================

TEST_F(RUDPPacketProcessorTest, OnRecvPacket_SizeMismatch_EarlyReturn_TpsNotIncremented)
{
    NetBuffer* buf = MakeMismatchedSizeBuffer();
    const auto validAddr = MakeValidAddrBuffer();

    processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr));

    EXPECT_EQ(processor->GetTPS(), 0);
    NetBuffer::Free(buf);
}

TEST_F(RUDPPacketProcessorTest, OnRecvPacket_SizeMismatch_DelegateNeverCalled)
{
    NetBuffer* buf = MakeMismatchedSizeBuffer();
    const auto validAddr = MakeValidAddrBuffer();

    processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr));

    // ProcessByPacketType 미진입 → 어떤 delegate 메서드도 호출되지 않아야 함
    EXPECT_EQ(mockDelegate.onRecvPacketCount, 0);
    EXPECT_EQ(mockDelegate.tryConnectCount,   0);
    EXPECT_EQ(mockDelegate.onSendReplyCount,  0);
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
// 5. OnRecvPacket — addrBuffer 가 sizeof(sockaddr_in) 보다 작을 때
//    두 번째 guard 에서 반환 (size 체크 통과 후)
//    ※ MakePassthroughBuffer 로 size 체크를 확실히 통과시킨 뒤 테스트
// ============================================================

TEST_F(RUDPPacketProcessorTest, OnRecvPacket_TooSmallAddrBuffer_EarlyReturn_TpsNotIncremented)
{
    NetBuffer* buf = MakePassthroughBuffer(); // size 체크 통과 보장
    const auto smallAddr = MakeTooSmallAddrBuffer();

    processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(smallAddr));

    EXPECT_EQ(processor->GetTPS(), 0);
    NetBuffer::Free(buf);
}

TEST_F(RUDPPacketProcessorTest, OnRecvPacket_TooSmallAddrBuffer_DelegateNeverCalled)
{
    NetBuffer* buf = MakePassthroughBuffer();
    const auto smallAddr = MakeTooSmallAddrBuffer();

    processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(smallAddr));

    EXPECT_EQ(mockDelegate.onRecvPacketCount, 0);
    EXPECT_EQ(mockDelegate.tryConnectCount,   0);
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
// 6. OnRecvPacket — addrBuffer 경계값
//    sizeof(sockaddr_in) - 1 : 실패 (too small)
//    sizeof(sockaddr_in)     : 통과 (ProcessByPacketType 진입, null keyHandle 에서 반환)
// ============================================================

TEST_F(RUDPPacketProcessorTest, OnRecvPacket_AddrBuffer_OneLessThanRequired_EarlyReturn)
{
    NetBuffer* buf = MakePassthroughBuffer();
    const std::vector<unsigned char> oneLess(sizeof(sockaddr_in) - 1, 0);

    processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(oneLess));

    EXPECT_EQ(mockDelegate.onRecvPacketCount, 0);
    EXPECT_EQ(mockDelegate.tryConnectCount,   0);
    NetBuffer::Free(buf);
}

TEST_F(RUDPPacketProcessorTest, OnRecvPacket_AddrBuffer_ExactRequiredSize_PassesSizeCheck_NoCrash)
{
    // sizeof(sockaddr_in) 정확히 맞으면 addrBuffer 체크를 통과해서
    // ProcessByPacketType 로 진입한다. null keyHandle 에서 안전하게 반환.
    NetBuffer* buf = MakePassthroughBuffer();
    const auto exactAddr = MakeValidAddrBuffer(); // exactly sizeof(sockaddr_in)

    EXPECT_NO_THROW(
        processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(exactAddr)));

    NetBuffer::Free(buf);
}

// ============================================================
// 7. OnRecvPacket — size 체크 통과 + addrBuffer 정상
//    ProcessByPacketType 진입 → null sessionKeyHandle 검사에서 반환
//    dummyKeyHandle == nullptr (MockSessionDelegate 기본값)
//    → step 2 (null 검사) 에서 LOG_ERROR 후 즉시 반환
//    → switch 분기(step 3)에는 도달하지 않음
// ============================================================

TEST_F(RUDPPacketProcessorTest, OnRecvPacket_NullKeyHandle_NoCrash)
{
    // dummyKeyHandle = nullptr (기본값)
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

TEST_F(RUDPPacketProcessorTest, OnRecvPacket_NullKeyHandle_DelegateOnRecvNotCalled)
{
    NetBuffer* buf = MakePassthroughBuffer();
    const auto validAddr = MakeValidAddrBuffer();

    processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr));

    // null keyHandle 로 인해 ProcessByPacketType 이 조기 반환
    // → OnRecvPacket delegate 는 호출되지 않아야 함
    EXPECT_EQ(mockDelegate.onRecvPacketCount, 0);
    NetBuffer::Free(buf);
}

TEST_F(RUDPPacketProcessorTest, OnRecvPacket_NullKeyHandle_TryConnectNotCalled)
{
    NetBuffer* buf = MakePassthroughBuffer();
    const auto validAddr = MakeValidAddrBuffer();

    processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr));

    EXPECT_EQ(mockDelegate.tryConnectCount, 0);
    NetBuffer::Free(buf);
}

// ============================================================
// 8. OnRecvPacket — 반복 호출 안전성
//    매 호출마다 크래시 없이 TPS = 0 을 유지
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

// ============================================================
// 9. OnRecvPacket — size 불일치 반복 호출 안전성
// ============================================================

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
    EXPECT_EQ(processor->GetTPS(), 0);
}

// ============================================================
// 10. 경계 조합: size 불일치 → size 일치로 전환해도 상태 오염 없음
//     이전 실패 호출이 이후 정상 호출에 영향을 주지 않아야 한다
// ============================================================

TEST_F(RUDPPacketProcessorTest, OnRecvPacket_MismatchThenPassthrough_NoCrossContamination)
{
    const auto validAddr = MakeValidAddrBuffer();

    // 첫 번째 호출: size 불일치
    {
        NetBuffer* buf = MakeMismatchedSizeBuffer();
        processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr));
        NetBuffer::Free(buf);
    }
    EXPECT_EQ(processor->GetTPS(), 0);
    EXPECT_EQ(mockDelegate.onRecvPacketCount, 0);

    // 두 번째 호출: size 일치 → ProcessByPacketType 진입 → null keyHandle 에서 반환
    {
        NetBuffer* buf = MakePassthroughBuffer();
        EXPECT_NO_THROW(
            processor->OnRecvPacket(session, *buf, std::span<const unsigned char>(validAddr)));
        NetBuffer::Free(buf);
    }
    EXPECT_EQ(processor->GetTPS(), 0);
    EXPECT_EQ(mockDelegate.onRecvPacketCount, 0);
}
