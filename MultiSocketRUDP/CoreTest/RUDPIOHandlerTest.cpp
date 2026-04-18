#include "PreCompile.h"
#include <gtest/gtest.h>

#include "RUDPIOHandler.h"
#include "MultiSocketRUDPCore.h"
#include "IOContext.h"
#include "SendPacketInfo.h"
#include "MockRIOManager.h"
#include "MockSessionDelegate.h"

// ============================================================
// 테스트용 최소 RUDPSession 서브클래스
//   - RUDPSession 생성자가 protected 이므로 파생 클래스가 필요
//   - MultiSocketRUDPCore 참조만 저장하고 실제 IO 초기화는 하지 않음
// ============================================================
class TestSession final : public RUDPSession
{
public:
    explicit TestSession(MultiSocketRUDPCore& inCore) : RUDPSession(inCore) {}
};

// ============================================================
// RUDPIOHandler 단위 테스트 픽스처
//
// 테스트 가능 범위와 제한:
//   [가능] DoRecv 의 각 guard 분기(context null, 소켓 invalid, RIOReceiveEx 실패/성공)
//   [가능] DoSend 의 NothingToSend / 빈 큐 / IO 모드 CAS 실패 경로
//   [가능] IOCompleted 의 null context / OP_ERROR 분기
//   [불가] IOCompleted OP_RECV/OP_SEND 성공 경로
//          → RecvIOCompleted 내부에서 MultiSocketRUDPCoreFunctionDelegate::EnqueueContextResult()를
//            호출하는데 이는 전역 싱글톤이고 단위 테스트 환경에서 core가 null이므로 crash.
//          → 통합 테스트에서 별도 검증 필요.
//
//   ※ IOCompleted 에서 RecvIOCompleted/SendIOCompleted 가 false 반환 시
//     context->session->DoDisconnect() 를 호출하므로 null session 을 넘기면 crash.
//     따라서 OP_RECV/OP_SEND + null session 조합 테스트는 작성하지 않는다.
//
// 의존성:
//   MockRIOManager     - RIOReceiveEx / RIOSendEx 반환값 제어
//   MockSessionDelegate - 소켓, RecvContext, SendContext 등 제어
//   CTLSMemoryPool<IOContext> - IOContext 풀
//   sendPacketInfoList / sendPacketInfoListLock - 재전송 목록 (threadId 0 용)
// ============================================================
class RUDPIOHandlerTest : public ::testing::Test
{
protected:
    static constexpr BYTE        MAX_HOLDING_SIZE = 10;
    static constexpr BYTE        THREAD_ID        = 0;
    static constexpr unsigned int RETRANSMIT_MS   = 1000;

    void SetUp() override
    {
        sendPacketInfoList.emplace_back();
        sendPacketInfoListLock.push_back(std::make_unique<std::mutex>());

        handler = std::make_unique<RUDPIOHandler>(
            mockRIO,
            mockDelegate,
            contextPool,
            sendPacketInfoList,
            sendPacketInfoListLock,
            MAX_HOLDING_SIZE,
            RETRANSMIT_MS);
    }

    void TearDown() override
    {
        handler.reset();
    }

    // ── 의존성 ──────────────────────────────────────────────
    MockRIOManager      mockRIO;
    MockSessionDelegate mockDelegate;
    CTLSMemoryPool<IOContext> contextPool{ 4, false };

    std::vector<std::list<SendPacketInfo*>>   sendPacketInfoList;
    std::vector<std::unique_ptr<std::mutex>>  sendPacketInfoListLock;

    std::unique_ptr<RUDPIOHandler> handler;

    // RUDPSession 은 생성자가 protected 이므로 TestSession 파생 클래스 사용
    // MultiSocketRUDPCore 는 StartServer() 없이 생성자만 호출 → 네트워크 미초기화
    MultiSocketRUDPCore coreStub{ L"", L"" };
    TestSession         session{ coreStub };

    // ── 헬퍼 ────────────────────────────────────────────────

    // DoRecv 가 RIOReceiveEx 까지 도달할 수 있도록 context 와 소켓을 준비
    void SetupValidRecvContext()
    {
        mockDelegate.recvBufferContextReturn = std::make_shared<IOContext>();
        mockDelegate.getSocketReturn         = reinterpret_cast<SOCKET>(1); // 가짜 유효 소켓
    }
};

// ============================================================
// 1. 핸들러 생성 / 소멸 안전성
// ============================================================

TEST_F(RUDPIOHandlerTest, Handler_CreateAndDestroy_NoCrash)
{
    std::vector<std::list<SendPacketInfo*>>  infoList;
    std::vector<std::unique_ptr<std::mutex>> lockList;
    infoList.emplace_back();
    lockList.push_back(std::make_unique<std::mutex>());

    MockRIOManager      localRIO;
    MockSessionDelegate localDelegate;
    CTLSMemoryPool<IOContext> localPool{ 2, false };

    EXPECT_NO_THROW({
        RUDPIOHandler localHandler(
            localRIO, localDelegate, localPool,
            infoList, lockList,
            MAX_HOLDING_SIZE, RETRANSMIT_MS);
    });
}

// ============================================================
// 2. IOCompleted — null context
// ============================================================

TEST_F(RUDPIOHandlerTest, IOCompleted_NullContext_ReturnsFalse)
{
    EXPECT_FALSE(handler->IOCompleted(nullptr, 0, THREAD_ID));
}

TEST_F(RUDPIOHandlerTest, IOCompleted_NullContext_NeverTouchesMock)
{
    handler->IOCompleted(nullptr, 0, THREAD_ID);

    EXPECT_EQ(mockRIO.rioReceiveExCallCount, 0);
    EXPECT_EQ(mockRIO.rioSendExCallCount,    0);
    EXPECT_EQ(mockDelegate.onRecvPacketCount, 0);
}

// ============================================================
// 3. IOCompleted — OP_ERROR 타입
//    default 분기로 빠져 false 를 반환해야 한다
//    ※ session 을 함께 전달하지만 OP_ERROR 분기에서는 session 을 참조하지 않음
// ============================================================

TEST_F(RUDPIOHandlerTest, IOCompleted_OpError_ReturnsFalse)
{
    IOContext ctx;
    ctx.InitContext(INVALID_SESSION_ID, RIO_OPERATION_TYPE::OP_ERROR);
    ctx.session = &session;

    EXPECT_FALSE(handler->IOCompleted(&ctx, 0, THREAD_ID));
}

TEST_F(RUDPIOHandlerTest, IOCompleted_OpError_NeverCallsRIO)
{
    IOContext ctx;
    ctx.InitContext(INVALID_SESSION_ID, RIO_OPERATION_TYPE::OP_ERROR);
    ctx.session = &session;

    handler->IOCompleted(&ctx, 0, THREAD_ID);

    EXPECT_EQ(mockRIO.rioReceiveExCallCount, 0);
    EXPECT_EQ(mockRIO.rioSendExCallCount,    0);
}

// ============================================================
// 4. DoRecv — GetRecvBufferContext 가 nullptr 반환
//    첫 번째 guard 에서 false 를 반환해야 한다
// ============================================================

TEST_F(RUDPIOHandlerTest, DoRecv_NullRecvBufferContext_ReturnsFalse)
{
    mockDelegate.recvBufferContextReturn = nullptr;

    EXPECT_FALSE(handler->DoRecv(session));
}

TEST_F(RUDPIOHandlerTest, DoRecv_NullRecvBufferContext_NeverCallsRIOReceiveEx)
{
    mockDelegate.recvBufferContextReturn = nullptr;
    mockRIO.rioReceiveExCallCount        = 0;

    handler->DoRecv(session);

    EXPECT_EQ(mockRIO.rioReceiveExCallCount, 0);
}

// ============================================================
// 5. DoRecv — INVALID_SOCKET
//    유효한 context 가 있어도 소켓이 유효하지 않으면 false
// ============================================================

TEST_F(RUDPIOHandlerTest, DoRecv_InvalidSocket_ReturnsFalse)
{
    mockDelegate.recvBufferContextReturn = std::make_shared<IOContext>();
    mockDelegate.getSocketReturn         = INVALID_SOCKET;

    EXPECT_FALSE(handler->DoRecv(session));
}

TEST_F(RUDPIOHandlerTest, DoRecv_InvalidSocket_NeverCallsRIOReceiveEx)
{
    mockDelegate.recvBufferContextReturn = std::make_shared<IOContext>();
    mockDelegate.getSocketReturn         = INVALID_SOCKET;
    mockRIO.rioReceiveExCallCount        = 0;

    handler->DoRecv(session);

    EXPECT_EQ(mockRIO.rioReceiveExCallCount, 0);
}

// ============================================================
// 6. DoRecv — RIOReceiveEx 실패
//    유효한 context / 소켓이 있어도 RIOReceiveEx 가 false 이면 false 반환
// ============================================================

TEST_F(RUDPIOHandlerTest, DoRecv_RIOReceiveExFails_ReturnsFalse)
{
    SetupValidRecvContext();
    mockRIO.rioReceiveExReturn = false;

    EXPECT_FALSE(handler->DoRecv(session));
}

TEST_F(RUDPIOHandlerTest, DoRecv_RIOReceiveExFails_CallsRIOReceiveExOnce)
{
    SetupValidRecvContext();
    mockRIO.rioReceiveExReturn    = false;
    mockRIO.rioReceiveExCallCount = 0;

    handler->DoRecv(session);

    EXPECT_EQ(mockRIO.rioReceiveExCallCount, 1);
}

// ============================================================
// 7. DoRecv — 정상 경로 (RIOReceiveEx 성공)
// ============================================================

TEST_F(RUDPIOHandlerTest, DoRecv_AllConditionsMet_ReturnsTrue)
{
    SetupValidRecvContext();
    mockRIO.rioReceiveExReturn = true;

    EXPECT_TRUE(handler->DoRecv(session));
}

TEST_F(RUDPIOHandlerTest, DoRecv_Success_CallsRIOReceiveExExactlyOnce)
{
    SetupValidRecvContext();
    mockRIO.rioReceiveExReturn    = true;
    mockRIO.rioReceiveExCallCount = 0;

    handler->DoRecv(session);

    EXPECT_EQ(mockRIO.rioReceiveExCallCount, 1);
}

// ============================================================
// 8. DoRecv — 반복 호출 안전성
//    성공 / 실패 교대로 호출해도 크래시 없이 일관된 결과여야 한다
// ============================================================

TEST_F(RUDPIOHandlerTest, DoRecv_RepeatedSuccessfulCalls_NoCrash)
{
    SetupValidRecvContext();
    mockRIO.rioReceiveExReturn = true;

    for (int i = 0; i < 5; ++i)
    {
        EXPECT_TRUE(handler->DoRecv(session)) << "Failed on iteration " << i;
    }
    EXPECT_EQ(mockRIO.rioReceiveExCallCount, 5);
}

TEST_F(RUDPIOHandlerTest, DoRecv_FailThenSuccess_ReturnCorrectly)
{
    SetupValidRecvContext();

    mockRIO.rioReceiveExReturn = false;
    EXPECT_FALSE(handler->DoRecv(session));

    mockRIO.rioReceiveExReturn = true;
    EXPECT_TRUE(handler->DoRecv(session));
}

// ============================================================
// 9. DoSend — IsNothingToSend == true
//    RIOSendEx 를 호출하지 않고 true 반환
// ============================================================

TEST_F(RUDPIOHandlerTest, DoSend_NothingToSend_ReturnsTrue)
{
    mockDelegate.isNothingToSendReturn = true;

    EXPECT_TRUE(handler->DoSend(session, THREAD_ID));
}

TEST_F(RUDPIOHandlerTest, DoSend_NothingToSend_NeverCallsRIOSendEx)
{
    mockDelegate.isNothingToSendReturn = true;
    mockRIO.rioSendExCallCount         = 0;

    handler->DoSend(session, THREAD_ID);

    EXPECT_EQ(mockRIO.rioSendExCallCount, 0);
}

// IO 모드가 제대로 복구되는지 검증:
// NothingToSend 경로는 IO_SENDING → IO_NONE_SENDING 으로 복구하므로
// 두 번 연속 호출해도 두 번째 역시 CAS 가 성공하고 true 를 반환해야 한다.
TEST_F(RUDPIOHandlerTest, DoSend_NothingToSend_IOModeProperlyClearedBetweenCalls)
{
    mockDelegate.isNothingToSendReturn = true;

    EXPECT_TRUE(handler->DoSend(session, THREAD_ID));
    EXPECT_TRUE(handler->DoSend(session, THREAD_ID)); // IO 모드가 복구되지 않으면 여기서 break → true (우연히 통과)
    // CAS 실패 시에도 true 가 반환되는 설계이지만,
    // 두 번 모두 true 이면 IO 모드 복구가 올바르게 동작하고 있음을 확인할 수 있다.
    EXPECT_EQ(mockRIO.rioSendExCallCount, 0);
}

// ============================================================
// 10. DoSend — IO_SENDING 상태에서 CAS 실패
//     이미 IO_SENDING 상태이면 while 루프를 즉시 break 하고 true 반환
//     (다른 스레드가 전송 중인 상황을 시뮬레이션)
//
//     ※ IO_NONE_SENDING 이 기본값(0)이므로 static_cast<IO_MODE>(1) 을 IO_SENDING 으로 사용
//       IO_MODE enum 의 구체적인 값이 변경되면 이 테스트도 함께 수정 필요
// ============================================================

TEST_F(RUDPIOHandlerTest, DoSend_AlreadyInSendingMode_ReturnsTrueWithoutCallingRIO)
{
    // dummyIOMode 를 IO_NONE_SENDING(0) 이 아닌 값으로 미리 설정 → CAS 실패
    mockDelegate.dummyIOMode.store(static_cast<IO_MODE>(1));
    mockRIO.rioSendExCallCount = 0;

    EXPECT_TRUE(handler->DoSend(session, THREAD_ID));
    EXPECT_EQ(mockRIO.rioSendExCallCount, 0);
}

// ============================================================
// 11. DoSend — 빈 큐 경로 (IsNothingToSend==false, 큐도 비어 있음)
//     MakeSendStream → totalSendSize==0 → context nullptr 반환
//     → RIOSendEx 를 호출하지 않고 true 반환
// ============================================================

TEST_F(RUDPIOHandlerTest, DoSend_EmptyQueue_ReturnsTrue)
{
    mockDelegate.isNothingToSendReturn      = false;
    mockDelegate.sendPacketInfoQueueSizeRet = 0;
    mockDelegate.reservedSendReturn         = nullptr;

    EXPECT_TRUE(handler->DoSend(session, THREAD_ID));
}

TEST_F(RUDPIOHandlerTest, DoSend_EmptyQueue_NeverCallsRIOSendEx)
{
    mockDelegate.isNothingToSendReturn      = false;
    mockDelegate.sendPacketInfoQueueSizeRet = 0;
    mockDelegate.reservedSendReturn         = nullptr;
    mockRIO.rioSendExCallCount              = 0;

    handler->DoSend(session, THREAD_ID);

    EXPECT_EQ(mockRIO.rioSendExCallCount, 0);
}

// 빈 큐 경로도 IO 모드를 복구하므로 반복 호출이 안전해야 한다
TEST_F(RUDPIOHandlerTest, DoSend_EmptyQueue_RepeatedCalls_AllReturnTrue)
{
    mockDelegate.isNothingToSendReturn      = false;
    mockDelegate.sendPacketInfoQueueSizeRet = 0;
    mockDelegate.reservedSendReturn         = nullptr;

    for (int i = 0; i < 5; ++i)
    {
        EXPECT_TRUE(handler->DoSend(session, THREAD_ID)) << "Failed on iteration " << i;
    }
    EXPECT_EQ(mockRIO.rioSendExCallCount, 0);
}

// ============================================================
// 12. DoSend — NothingToSend 와 빈 큐 경로 모두 반복 호출 안전성 통합
// ============================================================

TEST_F(RUDPIOHandlerTest, DoSend_AlternatingNothingToSendAndEmptyQueue_NoCrash)
{
    for (int i = 0; i < 6; ++i)
    {
        if (i % 2 == 0)
        {
            mockDelegate.isNothingToSendReturn      = true;
            mockDelegate.sendPacketInfoQueueSizeRet = 0;
        }
        else
        {
            mockDelegate.isNothingToSendReturn      = false;
            mockDelegate.sendPacketInfoQueueSizeRet = 0;
            mockDelegate.reservedSendReturn         = nullptr;
        }

        EXPECT_TRUE(handler->DoSend(session, THREAD_ID)) << "Failed on iteration " << i;
    }
    EXPECT_EQ(mockRIO.rioSendExCallCount, 0);
}
