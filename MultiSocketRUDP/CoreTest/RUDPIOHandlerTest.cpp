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
// 테스트 가능 범위:
//   [가능] IOCompleted — null context, OP_ERROR
//   [가능] IOCompleted OP_SEND — SendIOCompleted 경로
//          (SendIOCompleted는 EnqueueContextResult 를 호출하지 않으므로 안전)
//   [불가] IOCompleted OP_RECV 성공 경로
//          RecvIOCompleted 내부에서
//          MultiSocketRUDPCoreFunctionDelegate::EnqueueContextResult()를 호출하며
//          이 함수는 assert(core != nullptr)를 통과해야 하는데
//          단위 테스트 환경에서 core 싱글톤이 초기화되지 않으므로 crash.
//   [불가] IOCompleted OP_RECV/OP_SEND + null session
//          RecvIOCompleted/SendIOCompleted 가 false 반환 시
//          IOCompleted 내부에서 context->session->DoDisconnect() 호출 → crash.
//   [가능] DoRecv — 각 guard 분기 (context null, 소켓 invalid, RIOReceiveEx 실패/성공)
//   [가능] DoSend — NothingToSend / IO_SENDING CAS 실패 / 빈 큐 경로
//
// 의존성:
//   MockRIOManager      - RIOReceiveEx / RIOSendEx 반환값 제어
//   MockSessionDelegate - 소켓, RecvContext, SendContext 등 제어
//   CTLSMemoryPool<IOContext> - IOContext 풀 (OP_SEND 테스트에서 직접 사용)
//   sendPacketInfoList / sendPacketInfoListLock - 재전송 목록 (threadId 0 전용)
// ============================================================
class RUDPIOHandlerTest : public ::testing::Test
{
protected:
    static constexpr BYTE        MAX_HOLDING_SIZE = 10;
    static constexpr BYTE        THREAD_ID = 0;
    static constexpr unsigned int RETRANSMIT_MS = 1000;

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
    CTLSMemoryPool<IOContext> contextPool{ 8, false };

    std::vector<std::list<SendPacketInfo*>>   sendPacketInfoList;
    std::vector<std::unique_ptr<std::mutex>>  sendPacketInfoListLock;

    std::unique_ptr<RUDPIOHandler> handler;

    // MultiSocketRUDPCore: StartServer() 없이 생성자만 호출 → 네트워크 미초기화
    MultiSocketRUDPCore coreStub{ L"", L"" };
    TestSession         session{ coreStub };

    // ── 헬퍼 ────────────────────────────────────────────────

    // DoRecv 가 RIOReceiveEx 까지 도달할 수 있도록 context 와 소켓을 준비
    void SetupValidRecvContext()
    {
        mockDelegate.recvBufferContextReturn = std::make_shared<IOContext>();
        mockDelegate.getSocketReturn = SOCKET(1);
    }

    // IOCompleted OP_SEND 테스트에서 사용할 context 를 pool 에서 할당
    // ※ SendIOCompleted 내부에서 contextPool.Free(ctx) 를 호출하므로
    //    반환된 포인터를 테스트 코드에서 별도로 Free 해서는 안 된다
    IOContext* AllocSendContext()
    {
        IOContext* ctx = contextPool.Alloc();
        EXPECT_NE(ctx, nullptr);
        ctx->InitContext(INVALID_SESSION_ID, RIO_OPERATION_TYPE::OP_SEND);
        ctx->session = &session;
        return ctx;
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

TEST_F(RUDPIOHandlerTest, IOCompleted_NullContext_NeverTouchesMocks)
{
    std::ignore = handler->IOCompleted(nullptr, 0, THREAD_ID);

    EXPECT_EQ(mockRIO.rioReceiveExCallCount, 0);
    EXPECT_EQ(mockRIO.rioSendExCallCount, 0);
    EXPECT_EQ(mockDelegate.onRecvPacketCount, 0);
}

// ============================================================
// 3. IOCompleted — OP_ERROR 타입
//    default 분기로 빠져 false 를 반환해야 한다
//    ※ OP_ERROR 분기에서는 session 에 접근하지 않으므로
//       session 을 함께 전달해도 안전하다
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

    std::ignore = handler->IOCompleted(&ctx, 0, THREAD_ID);

    EXPECT_EQ(mockRIO.rioReceiveExCallCount, 0);
    EXPECT_EQ(mockRIO.rioSendExCallCount, 0);
}

// ============================================================
// 4. IOCompleted — OP_SEND (SendIOCompleted 경로)
//
// SendIOCompleted 흐름:
//   ① session null 검사 (통과)
//   ② IO 모드를 IO_NONE_SENDING 으로 강제 복구
//   ③ session.IsReleasing() 검사
//   ④ DoSend(session, threadId) 호출
//   ⑤ contextPool.Free(context) — 풀에서 할당한 context 를 내부에서 해제
//
// ※ context 는 반드시 contextPool.Alloc() 으로 할당해야 한다.
//   SendIOCompleted 내부의 contextPool.Free(context) 가 정상 동작하고,
//   테스트 코드에서 ctx 를 별도로 Free 하면 double-free 가 된다.
// ============================================================

TEST_F(RUDPIOHandlerTest, IOCompleted_OpSend_NothingToSend_ReturnsTrue)
{
    IOContext* ctx = AllocSendContext();
    mockDelegate.isNothingToSendReturn = true;

    // ctx 는 SendIOCompleted 내부에서 해제된다 — 이후 접근 금지
    EXPECT_TRUE(handler->IOCompleted(ctx, 0, THREAD_ID));
}

TEST_F(RUDPIOHandlerTest, IOCompleted_OpSend_NothingToSend_RioSendExNotCalled)
{
    IOContext* ctx = AllocSendContext();
    mockDelegate.isNothingToSendReturn = true;
    mockRIO.rioSendExCallCount = 0;

    std::ignore = handler->IOCompleted(ctx, 0, THREAD_ID);

    EXPECT_EQ(mockRIO.rioSendExCallCount, 0);
}

// DoSend 의 emptyQueue 경로 (isNothingToSend=false, 큐 empty)
// → MakeSendContext 가 호출되어 context 를 pool 에서 내부 할당·해제한 뒤
//   totalSendSize=0 이므로 TryRIOSend 를 호출하지 않고 true 반환
TEST_F(RUDPIOHandlerTest, IOCompleted_OpSend_EmptyQueue_ReturnsTrue)
{
    IOContext* ctx = AllocSendContext();
    mockDelegate.isNothingToSendReturn = false;
    mockDelegate.sendPacketInfoQueueSizeRet = 0;
    mockDelegate.reservedSendReturn = nullptr;

    EXPECT_TRUE(handler->IOCompleted(ctx, 0, THREAD_ID));
    EXPECT_EQ(mockRIO.rioSendExCallCount, 0);
}

// IOCompleted OP_SEND 연속 호출 — 각 호출마다 context 를 pool 에서 별도로 할당
TEST_F(RUDPIOHandlerTest, IOCompleted_OpSend_MultipleCalls_AllReturnTrue)
{
    mockDelegate.isNothingToSendReturn = true;

    for (int i = 0; i < 3; ++i)
    {
        IOContext* ctx = AllocSendContext();
        EXPECT_TRUE(handler->IOCompleted(ctx, 0, THREAD_ID))
            << "Failed on iteration " << i;
    }
    EXPECT_EQ(mockRIO.rioSendExCallCount, 0);
}

// ============================================================
// 5. DoRecv — GetRecvBufferContext 가 nullptr 반환
// ============================================================

TEST_F(RUDPIOHandlerTest, DoRecv_NullRecvBufferContext_ReturnsFalse)
{
    mockDelegate.recvBufferContextReturn = nullptr;

    EXPECT_FALSE(handler->DoRecv(session));
}

TEST_F(RUDPIOHandlerTest, DoRecv_NullRecvBufferContext_NeverCallsRIOReceiveEx)
{
    mockDelegate.recvBufferContextReturn = nullptr;
    mockRIO.rioReceiveExCallCount = 0;

    std::ignore = handler->DoRecv(session);

    EXPECT_EQ(mockRIO.rioReceiveExCallCount, 0);
}

// ============================================================
// 6. DoRecv — INVALID_SOCKET
// ============================================================

TEST_F(RUDPIOHandlerTest, DoRecv_InvalidSocket_ReturnsFalse)
{
    mockDelegate.recvBufferContextReturn = std::make_shared<IOContext>();
    mockDelegate.getSocketReturn = INVALID_SOCKET;

    EXPECT_FALSE(handler->DoRecv(session));
}

TEST_F(RUDPIOHandlerTest, DoRecv_InvalidSocket_NeverCallsRIOReceiveEx)
{
    mockDelegate.recvBufferContextReturn = std::make_shared<IOContext>();
    mockDelegate.getSocketReturn = INVALID_SOCKET;
    mockRIO.rioReceiveExCallCount = 0;

    std::ignore = handler->DoRecv(session);

    EXPECT_EQ(mockRIO.rioReceiveExCallCount, 0);
}

// ============================================================
// 7. DoRecv — RIOReceiveEx 실패
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
    mockRIO.rioReceiveExReturn = false;
    mockRIO.rioReceiveExCallCount = 0;

    std::ignore = handler->DoRecv(session);

    EXPECT_EQ(mockRIO.rioReceiveExCallCount, 1);
}

// ============================================================
// 8. DoRecv — 정상 경로
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
    mockRIO.rioReceiveExReturn = true;
    mockRIO.rioReceiveExCallCount = 0;

    std::ignore = handler->DoRecv(session);

    EXPECT_EQ(mockRIO.rioReceiveExCallCount, 1);
}

// ============================================================
// 9. DoRecv — 반복 / 상태 전환 안전성
// ============================================================

TEST_F(RUDPIOHandlerTest, DoRecv_RepeatedSuccessfulCalls_CountAccumulates)
{
    SetupValidRecvContext();
    mockRIO.rioReceiveExReturn = true;
    mockRIO.rioReceiveExCallCount = 0;

    for (int i = 0; i < 5; ++i)
    {
        EXPECT_TRUE(handler->DoRecv(session)) << "Failed on iteration " << i;
    }
    EXPECT_EQ(mockRIO.rioReceiveExCallCount, 5);
}

TEST_F(RUDPIOHandlerTest, DoRecv_FailThenSuccess_ReturnsCorrectly)
{
    SetupValidRecvContext();

    mockRIO.rioReceiveExReturn = false;
    EXPECT_FALSE(handler->DoRecv(session));

    mockRIO.rioReceiveExReturn = true;
    EXPECT_TRUE(handler->DoRecv(session));

    EXPECT_EQ(mockRIO.rioReceiveExCallCount, 2);
}

// ============================================================
// 10. DoSend — IsNothingToSend == true
//     RIOSendEx 를 호출하지 않고 true 반환
// ============================================================

TEST_F(RUDPIOHandlerTest, DoSend_NothingToSend_ReturnsTrue)
{
    mockDelegate.isNothingToSendReturn = true;

    EXPECT_TRUE(handler->DoSend(session, THREAD_ID));
}

TEST_F(RUDPIOHandlerTest, DoSend_NothingToSend_NeverCallsRIOSendEx)
{
    mockDelegate.isNothingToSendReturn = true;
    mockRIO.rioSendExCallCount = 0;

    std::ignore = handler->DoSend(session, THREAD_ID);

    EXPECT_EQ(mockRIO.rioSendExCallCount, 0);
}

// NothingToSend 경로는 IO_NONE_SENDING 으로 모드를 복구한다.
// 복구가 올바르게 동작하면 두 번째 호출에서도 CAS 가 성공해 true 를 반환해야 한다.
TEST_F(RUDPIOHandlerTest, DoSend_NothingToSend_IOModeRestoredBetweenCalls)
{
    mockDelegate.isNothingToSendReturn = true;

    EXPECT_TRUE(handler->DoSend(session, THREAD_ID));
    EXPECT_TRUE(handler->DoSend(session, THREAD_ID));
    EXPECT_EQ(mockRIO.rioSendExCallCount, 0);
}

// ============================================================
// 11. DoSend — IO_SENDING 상태에서 CAS 실패
//     이미 다른 스레드가 전송 중인 상황을 시뮬레이션
//     while 루프를 즉시 break 하고 true 를 반환해야 한다
// ============================================================

TEST_F(RUDPIOHandlerTest, DoSend_AlreadyInSendingMode_ReturnsTrueWithoutCallingRIO)
{
    // IO_SENDING 상태를 직접 설정 → CAS(IO_NONE_SENDING → IO_SENDING) 실패
    mockDelegate.dummyIOMode.store(IO_MODE::IO_SENDING, std::memory_order_relaxed);
    mockRIO.rioSendExCallCount = 0;

    EXPECT_TRUE(handler->DoSend(session, THREAD_ID));
    EXPECT_EQ(mockRIO.rioSendExCallCount, 0);
}

// IO_SENDING 상태에서는 IsNothingToSend 조차 호출하지 않아야 한다
TEST_F(RUDPIOHandlerTest, DoSend_AlreadyInSendingMode_NeverChecksNothingToSend)
{
    // isNothingToSendReturn 을 false 로 해 두어도 CAS 실패로 즉시 break
    mockDelegate.dummyIOMode.store(IO_MODE::IO_SENDING, std::memory_order_relaxed);
    mockDelegate.isNothingToSendReturn = false;

    // RIOSendEx 가 호출되지 않으면 올바르게 break 한 것
    EXPECT_TRUE(handler->DoSend(session, THREAD_ID));
    EXPECT_EQ(mockRIO.rioSendExCallCount, 0);
}

// ============================================================
// 12. DoSend — 빈 큐 경로 (IsNothingToSend==false, 큐 실제로 비어 있음)
//     MakeSendContext 를 호출하지만 totalSendSize==0 → context nullptr
//     → TryRIOSend 미호출, true 반환
// ============================================================

TEST_F(RUDPIOHandlerTest, DoSend_EmptyQueue_ReturnsTrue)
{
    mockDelegate.isNothingToSendReturn = false;
    mockDelegate.sendPacketInfoQueueSizeRet = 0;
    mockDelegate.reservedSendReturn = nullptr;

    EXPECT_TRUE(handler->DoSend(session, THREAD_ID));
}

TEST_F(RUDPIOHandlerTest, DoSend_EmptyQueue_NeverCallsRIOSendEx)
{
    mockDelegate.isNothingToSendReturn = false;
    mockDelegate.sendPacketInfoQueueSizeRet = 0;
    mockDelegate.reservedSendReturn = nullptr;
    mockRIO.rioSendExCallCount = 0;

    std::ignore = handler->DoSend(session, THREAD_ID);

    EXPECT_EQ(mockRIO.rioSendExCallCount, 0);
}

// 빈 큐 경로도 IO 모드를 복구하므로 반복 호출이 안전해야 한다
TEST_F(RUDPIOHandlerTest, DoSend_EmptyQueue_RepeatedCalls_AllReturnTrue)
{
    mockDelegate.isNothingToSendReturn = false;
    mockDelegate.sendPacketInfoQueueSizeRet = 0;
    mockDelegate.reservedSendReturn = nullptr;

    for (int i = 0; i < 5; ++i)
    {
        EXPECT_TRUE(handler->DoSend(session, THREAD_ID)) << "Failed on iteration " << i;
    }
    EXPECT_EQ(mockRIO.rioSendExCallCount, 0);
}

// ============================================================
// 13. DoSend — 경로 교차 반복 안전성
//     NothingToSend 경로와 빈 큐 경로를 번갈아 호출해도 크래시 없음
// ============================================================

TEST_F(RUDPIOHandlerTest, DoSend_AlternatingPaths_NoCrash)
{
    for (int i = 0; i < 6; ++i)
    {
        if (i % 2 == 0)
        {
            mockDelegate.isNothingToSendReturn = true;
        }
        else
        {
            mockDelegate.isNothingToSendReturn = false;
            mockDelegate.sendPacketInfoQueueSizeRet = 0;
            mockDelegate.reservedSendReturn = nullptr;
        }

        EXPECT_TRUE(handler->DoSend(session, THREAD_ID)) << "Failed on iteration " << i;
    }
    EXPECT_EQ(mockRIO.rioSendExCallCount, 0);
}