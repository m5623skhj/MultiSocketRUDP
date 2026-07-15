#include "PreCompile.h"
#include <gtest/gtest.h>

#include "MultiSocketRUDPCore.h"
#include "RUDPSessionFunctionDelegate.h"
#include "RUDPSessionManager.h"
#include "MockSessionDelegate.h"
#include "RUDPSessionTestAccess.h"

namespace
{
	class ManagerTestSession final : public RUDPSession
	{
	public:
		explicit ManagerTestSession(MultiSocketRUDPCore& inCore) : RUDPSession(inCore) {}
		~ManagerTestSession() override { ++destroyedCount; }
		static inline std::atomic_int destroyedCount{};
	};
}

class RUDPSessionManagerTest : public ::testing::Test
{
protected:
	MultiSocketRUDPCore core{ L"", L"" };
	RUDPSessionFunctionDelegate delegate;
};

TEST_F(RUDPSessionManagerTest, AcquireExhaustsPoolWithUniqueSessionIds)
{
	constexpr unsigned short maxSessions = 8;
	RUDPSessionManager manager{ maxSessions, core, delegate };
	ASSERT_TRUE(manager.Initialize(3, [this](MultiSocketRUDPCore&) { return new ManagerTestSession(core); }));

	std::set<SessionIdType> ids;
	for (unsigned short i = 0; i < maxSessions; ++i)
	{
		auto* session = manager.AcquireSession();
		ASSERT_NE(session, nullptr);
		ids.emplace(session->GetSessionId());
		EXPECT_EQ(session->GetThreadId(), i % 3);
	}
	EXPECT_EQ(ids.size(), maxSessions);
	EXPECT_EQ(manager.GetUnusedSessionCount(), 0);
	EXPECT_EQ(manager.AcquireSession(), nullptr);
}

TEST_F(RUDPSessionManagerTest, ConcurrentAcquireNeverReturnsDuplicateSession)
{
	constexpr unsigned short maxSessions = 32;
	RUDPSessionManager manager{ maxSessions, core, delegate };
	ASSERT_TRUE(manager.Initialize(4, [this](MultiSocketRUDPCore&) { return new ManagerTestSession(core); }));

	std::mutex resultLock;
	std::vector<SessionIdType> ids;
	std::vector<std::jthread> threads;
	for (unsigned short i = 0; i < maxSessions; ++i)
	{
		threads.emplace_back([&]()
		{
			if (auto* session = manager.AcquireSession())
			{
				std::scoped_lock lock(resultLock);
				ids.push_back(session->GetSessionId());
			}
		});
	}
	threads.clear();

	std::set<SessionIdType> uniqueIds(ids.begin(), ids.end());
	EXPECT_EQ(ids.size(), maxSessions);
	EXPECT_EQ(uniqueIds.size(), maxSessions);
}

// 주어진 세션 ID가 유효하지 않을 때 RUDPSessionManager의 ReleaseSession 메서드가 실패하는지 테스트합니다.
// 세션이 해제되지 않으므로 GetUnusedSessionCount는 초기 maxSessions 값을 유지해야 합니다.
// 실패 조건: ReleaseSession은 유효하지 않은 세션 ID에 대해 false를 반환합니다.
// 상태 변화: GetUnusedSessionCount는 변경되지 않고 maxSessions와 동일하게 유지됩니다.
TEST_F(RUDPSessionManagerTest, ReleaseSessionRejectsInvalidSessionId)
{
	constexpr unsigned short maxSessions = 2;
	RUDPSessionManager manager{ maxSessions, core, delegate };
	ASSERT_TRUE(manager.Initialize(1, [this](MultiSocketRUDPCore&) { return new ManagerTestSession(core); }));

	EXPECT_FALSE(manager.ReleaseSession(maxSessions));
	EXPECT_EQ(manager.GetUnusedSessionCount(), maxSessions);
}

// 세션이 해제 대기 중이 아닐 때 RUDPSessionManager의 ReleaseSession 메서드가 세션을 해제하지 못하는지 테스트합니다.
// 세션을 획득한 후 ReleaseSession을 호출하면 실패해야 하며, 사용 중인 세션 수는 변경되지 않아야 합니다.
// 실패 조건: ReleaseSession은 해제 대기 중이 아닌 세션 ID에 대해 false를 반환합니다.
// 상태 변화: AcquireSession 호출 후 GetUnusedSessionCount는 0을 유지하며, ReleaseSession 호출에 의해 변경되지 않습니다.
TEST_F(RUDPSessionManagerTest, ReleaseSessionRejectsSessionThatIsNotReleasing)
{
	constexpr unsigned short maxSessions = 1;
	RUDPSessionManager manager{ maxSessions, core, delegate };
	ASSERT_TRUE(manager.Initialize(1, [this](MultiSocketRUDPCore&) { return new ManagerTestSession(core); }));

	auto* session = manager.AcquireSession();
	ASSERT_NE(session, nullptr);

	EXPECT_FALSE(manager.ReleaseSession(session->GetSessionId()));
	EXPECT_EQ(manager.GetUnusedSessionCount(), 0);
}

// 재전송으로 인한 연결 끊김 발생 시 RUDPSessionManager의 연결 통계가 정확하게 갱신되는지 확인합니다.
// IncrementConnectedCount 호출 후 DISCONNECT_REASON::BY_RETRANSMISSION으로 DecrementConnectedCount를 호출하면,
// 현재 세션 수는 0으로, 총 연결 수는 1로, 총 연결 끊김 수는 1로, 재전송으로 인한 연결 끊김 수는 1로 기록되어야 합니다.
// 상태 변화: GetNowSessionCount는 0이 되고, GetAllConnectedCount는 1u로, GetAllDisconnectedCount는 1u로, GetAllDisconnectedByRetransmissionCount는 1u로 업데이트됩니다.
TEST_F(RUDPSessionManagerTest, DecrementConnectedCountTracksRetransmissionDisconnect)
{
	RUDPSessionManager manager{ 0, core, delegate };
	manager.IncrementConnectedCount();

	manager.DecrementConnectedCount(DISCONNECT_REASON::BY_RETRANSMISSION);

	EXPECT_EQ(manager.GetNowSessionCount(), 0);
	EXPECT_EQ(manager.GetAllConnectedCount(), 1u);
	EXPECT_EQ(manager.GetAllDisconnectedCount(), 1u);
	EXPECT_EQ(manager.GetAllDisconnectedByRetransmissionCount(), 1u);
}

// DISCONNECT_REASON::BY_ABORT_RESERVED가 RUDPSessionManager의 세션 수 감소에 영향을 미치지 않는지 확인합니다.
// 이 종료 사유는 현재 세션 수에 영향을 주지 않아야 합니다.
// 상태 변화: IncrementConnectedCount 호출 후 DecrementConnectedCount(BY_ABORT_RESERVED)를 호출해도 GetNowSessionCount는 1을 유지하며,
// 연결 끊김 관련 통계는 업데이트되지 않습니다.
TEST_F(RUDPSessionManagerTest, DecrementConnectedCountIgnoresAbortReserved)
{
	RUDPSessionManager manager{ 0, core, delegate };
	manager.IncrementConnectedCount();

	manager.DecrementConnectedCount(DISCONNECT_REASON::BY_ABORT_RESERVED);

	EXPECT_EQ(manager.GetNowSessionCount(), 1);
	EXPECT_EQ(manager.GetAllConnectedCount(), 1u);
	EXPECT_EQ(manager.GetAllDisconnectedCount(), 0u);
	EXPECT_EQ(manager.GetAllDisconnectedByRetransmissionCount(), 0u);
}

// ------------------------------------------------------------
// 세션 factory가 없으면 초기화가 실패하고 세션 관리자가 미초기화 상태를 유지하는지 확인합니다.
// ------------------------------------------------------------
TEST_F(RUDPSessionManagerTest, InitializeRejectsNullFactoryAndRemainsUninitialized)
{
	RUDPSessionManager manager{ 2, core, delegate };
	SessionFactoryFunc factory;

	EXPECT_FALSE(manager.Initialize(1, std::move(factory)));
	EXPECT_FALSE(manager.IsInitialized());
	EXPECT_EQ(manager.GetUnusedSessionCount(), 0);
	EXPECT_EQ(manager.AcquireSession(), nullptr);
}

// ------------------------------------------------------------
// 이미 초기화된 관리자를 다시 초기화해도 세션 풀이 중복 생성되지 않는지 확인합니다.
// ------------------------------------------------------------
TEST_F(RUDPSessionManagerTest, RepeatedInitializeDoesNotRecreatePool)
{
	RUDPSessionManager manager{ 3, core, delegate };
	int factoryCallCount = 0;
	ASSERT_TRUE(manager.Initialize(1, [&](MultiSocketRUDPCore&)
	{
		++factoryCallCount;
		return new ManagerTestSession(core);
	}));

	EXPECT_TRUE(manager.Initialize(1, [&](MultiSocketRUDPCore&)
	{
		++factoryCallCount;
		return new ManagerTestSession(core);
	}));
	EXPECT_EQ(factoryCallCount, 3);
	EXPECT_EQ(manager.GetUnusedSessionCount(), 3);
}

// ------------------------------------------------------------
// 해제 중인 세션이 풀로 반환된 뒤 generation과 상태를 초기화하여 재사용되는지 확인합니다.
// ------------------------------------------------------------
TEST_F(RUDPSessionManagerTest, ReleasingSessionReturnsToPoolAndIsReinitializedForReuse)
{
	RUDPSessionManager manager{ 1, core, delegate };
	ASSERT_TRUE(manager.Initialize(1, [this](MultiSocketRUDPCore&) { return new ManagerTestSession(core); }));
	RUDPSession* session = manager.AcquireSession();
	ASSERT_NE(session, nullptr);
	const auto sessionId = session->GetSessionId();
	const auto generation = session->GetSessionGeneration();
	RUDPSessionBehaviorAccess::SetReleasing(*session);

	ASSERT_TRUE(manager.ReleaseSession(sessionId));
	EXPECT_EQ(manager.GetUnusedSessionCount(), 1);
	EXPECT_EQ(session->GetSessionState(), SESSION_STATE::DISCONNECTED);
	EXPECT_GT(session->GetSessionGeneration(), generation);

	RUDPSession* reused = manager.AcquireSession();
	EXPECT_EQ(reused, session);
	EXPECT_EQ(reused->GetSessionId(), sessionId);
}

// ------------------------------------------------------------
// 전체 종료와 정리가 모든 세션을 방문하고 관리자 상태와 보유 세션을 초기화하는지 확인합니다.
// ------------------------------------------------------------
TEST_F(RUDPSessionManagerTest, CloseAndClearVisitEverySessionAndResetManagerState)
{
	MockSessionDelegate mockDelegate;
	RUDPSessionManager manager{ 3, core, mockDelegate };
	ManagerTestSession::destroyedCount.store(0);
	ASSERT_TRUE(manager.Initialize(1, [this](MultiSocketRUDPCore&) { return new ManagerTestSession(core); }));
	manager.IncrementConnectedCount();

	manager.CloseAllSessions();
	EXPECT_EQ(mockDelegate.closeSocketCount, 3);
	EXPECT_EQ(manager.GetNowSessionCount(), 0);

	manager.ClearAllSessions();
	EXPECT_EQ(mockDelegate.recvContextResetCount, 3);
	EXPECT_EQ(ManagerTestSession::destroyedCount.load(), 3);
	EXPECT_FALSE(manager.IsInitialized());
	EXPECT_EQ(manager.GetUnusedSessionCount(), 0);
}

// ------------------------------------------------------------
// 상태 기반 조회 함수가 사용 중이거나 해제 중인 조건에 맞는 세션만 반환하는지 확인합니다.
// ------------------------------------------------------------
TEST_F(RUDPSessionManagerTest, StateFilteredGettersReturnOnlyUsingSessions)
{
	RUDPSessionManager manager{ 2, core, delegate };
	ASSERT_TRUE(manager.Initialize(1, [this](MultiSocketRUDPCore&) { return new ManagerTestSession(core); }));
	RUDPSession* reserved = manager.AcquireSession();
	ASSERT_NE(reserved, nullptr);
	RUDPSessionBehaviorAccess::SetReserved(*reserved);
	ASSERT_EQ(reserved->GetSessionId(), 0);

	EXPECT_EQ(manager.GetUsingSession(0), reserved);
	EXPECT_EQ(manager.GetReleasingSession(0), nullptr);
	EXPECT_EQ(manager.GetUsingSession(2), nullptr);
	EXPECT_EQ(manager.GetReleasingSession(2), nullptr);
}

// ------------------------------------------------------------
// heartbeat 검사가 연결 세션에는 heartbeat를 보내고 만료된 예약 세션은 중단하는지 확인합니다.
// ------------------------------------------------------------
TEST_F(RUDPSessionManagerTest, HeartbeatCheckRoutesConnectedAndTimedOutReservedSessions)
{
	MockSessionDelegate mockDelegate;
	mockDelegate.checkReservedTimeoutReturn = true;
	RUDPSessionManager manager{ 3, core, mockDelegate };
	ASSERT_TRUE(manager.Initialize(1, [this](MultiSocketRUDPCore&) { return new ManagerTestSession(core); }));
	RUDPSession* connected = manager.AcquireSession();
	RUDPSession* reserved = manager.AcquireSession();
	ASSERT_NE(connected, nullptr);
	ASSERT_NE(reserved, nullptr);
	RUDPSessionBehaviorAccess::SetConnected(*connected);
	RUDPSessionBehaviorAccess::SetReserved(*reserved);

	manager.HeartbeatCheck(1234);

	EXPECT_EQ(mockDelegate.sendHeartbeatCount, 1);
	EXPECT_EQ(mockDelegate.abortReservedCount, 1);
}

// ------------------------------------------------------------
// 제한 시간 전의 예약 세션은 heartbeat 검사에서 중단되지 않는지 확인합니다.
// ------------------------------------------------------------
TEST_F(RUDPSessionManagerTest, HeartbeatCheckDoesNotAbortReservedSessionBeforeTimeout)
{
	MockSessionDelegate mockDelegate;
	mockDelegate.checkReservedTimeoutReturn = false;
	RUDPSessionManager manager{ 1, core, mockDelegate };
	ASSERT_TRUE(manager.Initialize(1, [this](MultiSocketRUDPCore&) { return new ManagerTestSession(core); }));
	RUDPSession* reserved = manager.AcquireSession();
	ASSERT_NE(reserved, nullptr);
	RUDPSessionBehaviorAccess::SetReserved(*reserved);

	manager.HeartbeatCheck(99);

	EXPECT_EQ(mockDelegate.sendHeartbeatCount, 0);
	EXPECT_EQ(mockDelegate.abortReservedCount, 0);
}
