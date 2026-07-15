#include "gtest/gtest.h"
#include "SessionStateMachine.h"
#include <barrier>

// ============================================================
// SessionStateMachine 단위 테스트
//
// 상태 전이도:
//
//   [DISCONNECTED]
//	   │
//	   │ SetReserved()
//	   ▼
//   [RESERVED] ──TryAbortReserved() ──────────────
//	   │										 │
//	   │ TryTransitionToConnected()			 │
//	   ▼										 ▼
//   [CONNECTED] ──TryTransitionToReleasing()──▶[RELEASING]
//	   │										 │
//	   └──TryTransitionToReleasing()─────────────
//												 │
//										  SetDisconnected()
//												 │
//												 ▼
//										   [DISCONNECTED]
// ============================================================

class SessionStateMachineTest : public ::testing::Test
{
protected:
	SessionStateMachine sm;
};

// ------------------------------------------------------------
// 1. 초기 상태
// ------------------------------------------------------------
TEST_F(SessionStateMachineTest, InitialState_IsDisconnected)
{
	EXPECT_EQ(sm.GetSessionState(), SESSION_STATE::DISCONNECTED);
	EXPECT_FALSE(sm.IsConnected());
	EXPECT_FALSE(sm.IsReserved());
	EXPECT_FALSE(sm.IsReleasing());
	EXPECT_FALSE(sm.IsUsingSession());
}

// ------------------------------------------------------------
// 2. DISCONNECTED → RESERVED
// ------------------------------------------------------------
TEST_F(SessionStateMachineTest, SetReserved_TransitionsToReserved)
{
	sm.SetReserved();

	EXPECT_EQ(sm.GetSessionState(), SESSION_STATE::RESERVED);
	EXPECT_TRUE(sm.IsReserved());
	EXPECT_TRUE(sm.IsUsingSession());
	EXPECT_FALSE(sm.IsConnected());
	EXPECT_FALSE(sm.IsReleasing());
}

// ------------------------------------------------------------
// 3. RESERVED → CONNECTED
// ------------------------------------------------------------
TEST_F(SessionStateMachineTest, TryTransitionToConnected_FromReserved_Succeeds)
{
	sm.SetReserved();
	const bool result = sm.TryTransitionToConnected();

	EXPECT_TRUE(result);
	EXPECT_EQ(sm.GetSessionState(), SESSION_STATE::CONNECTED);
	EXPECT_TRUE(sm.IsConnected());
	EXPECT_TRUE(sm.IsUsingSession());
	EXPECT_FALSE(sm.IsReserved());
	EXPECT_FALSE(sm.IsReleasing());
}

TEST_F(SessionStateMachineTest, TryTransitionToConnected_FromDisconnected_Fails)
{
	const bool result = sm.TryTransitionToConnected();

	EXPECT_FALSE(result);
	EXPECT_EQ(sm.GetSessionState(), SESSION_STATE::DISCONNECTED);
}

TEST_F(SessionStateMachineTest, TryTransitionToConnected_AlreadyConnected_Fails)
{
	sm.SetReserved();
	std::ignore = sm.TryTransitionToConnected();

	const bool result = sm.TryTransitionToConnected();

	EXPECT_FALSE(result);
	EXPECT_TRUE(sm.IsConnected());
}

// ------------------------------------------------------------
// 4. RESERVED/CONNECTED → RELEASING (TryTransitionToReleasing)
// ------------------------------------------------------------
TEST_F(SessionStateMachineTest, TryTransitionToReleasing_FromReserved_Succeeds)
{
	sm.SetReserved();
	const bool result = sm.TryTransitionToReleasing();

	EXPECT_TRUE(result);
	EXPECT_EQ(sm.GetSessionState(), SESSION_STATE::RELEASING);
	EXPECT_TRUE(sm.IsReleasing());
	EXPECT_FALSE(sm.IsUsingSession());
}

TEST_F(SessionStateMachineTest, TryTransitionToReleasing_FromConnected_Succeeds)
{
	sm.SetReserved();
	std::ignore = sm.TryTransitionToConnected();
	const bool result = sm.TryTransitionToReleasing();

	EXPECT_TRUE(result);
	EXPECT_EQ(sm.GetSessionState(), SESSION_STATE::RELEASING);
	EXPECT_TRUE(sm.IsReleasing());
	EXPECT_FALSE(sm.IsUsingSession());
}

TEST_F(SessionStateMachineTest, TryTransitionToReleasing_FromDisconnected_Fails)
{
	const bool result = sm.TryTransitionToReleasing();

	EXPECT_FALSE(result);
	EXPECT_EQ(sm.GetSessionState(), SESSION_STATE::DISCONNECTED);
}

TEST_F(SessionStateMachineTest, TryTransitionToReleasing_AlreadyReleasing_Fails)
{
	sm.SetReserved();
	std::ignore = sm.TryTransitionToReleasing();

	const bool result = sm.TryTransitionToReleasing();

	EXPECT_FALSE(result);
	EXPECT_EQ(sm.GetSessionState(), SESSION_STATE::RELEASING);
}

// ------------------------------------------------------------
// 5. RESERVED → RELEASING (TryAbortReserved)
//	RESERVED 상태에서만 성공
// ------------------------------------------------------------
TEST_F(SessionStateMachineTest, TryAbortReserved_FromReserved_Succeeds)
{
	sm.SetReserved();
	const bool result = sm.TryAbortReserved();

	EXPECT_TRUE(result);
	EXPECT_EQ(sm.GetSessionState(), SESSION_STATE::RELEASING);
}

TEST_F(SessionStateMachineTest, TryAbortReserved_FromConnected_Fails)
{
	sm.SetReserved();
	std::ignore = sm.TryTransitionToConnected();

	const bool result = sm.TryAbortReserved();

	EXPECT_FALSE(result);
	EXPECT_TRUE(sm.IsConnected());
}

TEST_F(SessionStateMachineTest, TryAbortReserved_FromDisconnected_Fails)
{
	const bool result = sm.TryAbortReserved();

	EXPECT_FALSE(result);
	EXPECT_EQ(sm.GetSessionState(), SESSION_STATE::DISCONNECTED);
}

// ------------------------------------------------------------
// 6. RELEASING → DISCONNECTED (SetDisconnected)
// ------------------------------------------------------------
TEST_F(SessionStateMachineTest, SetDisconnected_AfterReleasing_TransitionsToDisconnected)
{
	sm.SetReserved();
	std::ignore = sm.TryTransitionToReleasing();
	sm.SetDisconnected();

	EXPECT_EQ(sm.GetSessionState(), SESSION_STATE::DISCONNECTED);
	EXPECT_FALSE(sm.IsReleasing());
	EXPECT_FALSE(sm.IsUsingSession());
	EXPECT_FALSE(sm.IsConnected());
	EXPECT_FALSE(sm.IsReserved());
}

// ------------------------------------------------------------
// 7. Reset
// ------------------------------------------------------------
TEST_F(SessionStateMachineTest, Reset_FromConnected_ResetsToDisconnected)
{
	sm.SetReserved();
	std::ignore = sm.TryTransitionToConnected();
	sm.Reset();

	EXPECT_EQ(sm.GetSessionState(), SESSION_STATE::DISCONNECTED);
	EXPECT_FALSE(sm.IsConnected());
	EXPECT_FALSE(sm.IsUsingSession());
}

TEST_F(SessionStateMachineTest, Reset_FromReleasing_ResetsToDisconnected)
{
	sm.SetReserved();
	std::ignore = sm.TryTransitionToReleasing();
	sm.Reset();

	EXPECT_EQ(sm.GetSessionState(), SESSION_STATE::DISCONNECTED);
}

// ------------------------------------------------------------
// 8. IsUsingSession: RESERVED || CONNECTED만 true
// ------------------------------------------------------------
TEST_F(SessionStateMachineTest, IsUsingSession_TrueOnlyForReservedAndConnected)
{
	EXPECT_FALSE(sm.IsUsingSession());

	sm.SetReserved();
	EXPECT_TRUE(sm.IsUsingSession());

	std::ignore = sm.TryTransitionToConnected();
	EXPECT_TRUE(sm.IsUsingSession());

	std::ignore = sm.TryTransitionToReleasing();
	EXPECT_FALSE(sm.IsUsingSession());

	sm.SetDisconnected();
	EXPECT_FALSE(sm.IsUsingSession());
}

// ------------------------------------------------------------
// 9. 전체 생명주기 시나리오
// ------------------------------------------------------------
TEST_F(SessionStateMachineTest, FullLifecycle_ReserveConnectRelease)
{
	EXPECT_EQ(sm.GetSessionState(), SESSION_STATE::DISCONNECTED);

	sm.SetReserved();
	EXPECT_TRUE(sm.IsReserved());

	EXPECT_TRUE(sm.TryTransitionToConnected());
	EXPECT_TRUE(sm.IsConnected());

	EXPECT_TRUE(sm.TryTransitionToReleasing());
	EXPECT_TRUE(sm.IsReleasing());

	sm.SetDisconnected();
	EXPECT_EQ(sm.GetSessionState(), SESSION_STATE::DISCONNECTED);
}

TEST_F(SessionStateMachineTest, FullLifecycle_ReserveAbort)
{
	sm.SetReserved();
	EXPECT_TRUE(sm.TryAbortReserved());
	EXPECT_TRUE(sm.IsReleasing());

	sm.SetDisconnected();
	EXPECT_EQ(sm.GetSessionState(), SESSION_STATE::DISCONNECTED);
}

TEST_F(SessionStateMachineTest, FullLifecycle_ResetAndReuse)
{
	sm.SetReserved();
	std::ignore = sm.TryTransitionToConnected();
	std::ignore = sm.TryTransitionToReleasing();
	sm.Reset();

	EXPECT_EQ(sm.GetSessionState(), SESSION_STATE::DISCONNECTED);

	sm.SetReserved();
	EXPECT_TRUE(sm.IsReserved());
	EXPECT_TRUE(sm.TryTransitionToConnected());
	EXPECT_TRUE(sm.IsConnected());
}

// ------------------------------------------------------------
// 여러 스레드가 동시에 연결 전이를 시도할 때 정확히 하나만 성공하는지 확인합니다.
// ------------------------------------------------------------
TEST(SessionStateMachineConcurrencyTest, ExactlyOneConcurrentConnectTransitionSucceeds)
{
	SessionStateMachine stateMachine;
	stateMachine.SetReserved();
	constexpr int threadCount = 8;
	std::barrier startBarrier(threadCount + 1);
	std::atomic_int successCount{};
	std::vector<std::jthread> threads;
	threads.reserve(threadCount);

	for (int i = 0; i < threadCount; ++i)
	{
		threads.emplace_back([&]()
		{
			startBarrier.arrive_and_wait();
			if (stateMachine.TryTransitionToConnected())
			{
				successCount.fetch_add(1, std::memory_order_relaxed);
			}
		});
	}

	startBarrier.arrive_and_wait();
	threads.clear();

	EXPECT_EQ(successCount.load(std::memory_order_relaxed), 1);
	EXPECT_EQ(stateMachine.GetSessionState(), SESSION_STATE::CONNECTED);
}
