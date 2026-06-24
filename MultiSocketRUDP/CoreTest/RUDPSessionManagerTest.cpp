#include "PreCompile.h"
#include <gtest/gtest.h>

#include "MultiSocketRUDPCore.h"
#include "RUDPSessionFunctionDelegate.h"
#include "RUDPSessionManager.h"

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

TEST_F(RUDPSessionManagerTest, ReleaseSessionRejectsInvalidSessionId)
{
	constexpr unsigned short maxSessions = 2;
	RUDPSessionManager manager{ maxSessions, core, delegate };
	ASSERT_TRUE(manager.Initialize(1, [this](MultiSocketRUDPCore&) { return new ManagerTestSession(core); }));

	EXPECT_FALSE(manager.ReleaseSession(maxSessions));
	EXPECT_EQ(manager.GetUnusedSessionCount(), maxSessions);
}

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
