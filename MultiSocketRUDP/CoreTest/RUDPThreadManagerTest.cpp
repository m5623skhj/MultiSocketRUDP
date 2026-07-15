#include "PreCompile.h"
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <latch>
#include <semaphore>

#include "RUDPThreadManager.h"
#include "../Common/etc/EnumTypes.h"

namespace
{
	using namespace std::chrono_literals;

	bool WaitUntil(const std::chrono::milliseconds timeout, const auto& predicate)
	{
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (std::chrono::steady_clock::now() < deadline)
		{
			if (predicate())
			{
				return true;
			}

			Sleep(1);
		}

		return predicate();
	}
}

TEST(RUDPThreadManagerTest, StartThreadsRunsRequestedThreadCountAndPassesThreadIndex)
{
	RUDPThreadManager manager;
	std::atomic_int startedCount{};
	std::array<std::atomic_bool, 3> seenIndex{};

	manager.StartThreads(THREAD_GROUP::IO_WORKER_THREAD, [&](const std::stop_token& stopToken, const unsigned char threadIndex)
	{
		seenIndex[threadIndex].store(true, std::memory_order_relaxed);
		startedCount.fetch_add(1, std::memory_order_relaxed);
		while (not stopToken.stop_requested())
		{
			Sleep(1);
		}
	}, 3);

	EXPECT_TRUE(WaitUntil(1s, [&]() { return startedCount.load(std::memory_order_relaxed) == 3; }));
	EXPECT_TRUE(seenIndex[0].load(std::memory_order_relaxed));
	EXPECT_TRUE(seenIndex[1].load(std::memory_order_relaxed));
	EXPECT_TRUE(seenIndex[2].load(std::memory_order_relaxed));

	manager.StopThreadGroup(THREAD_GROUP::IO_WORKER_THREAD);
}

TEST(RUDPThreadManagerTest, DuplicateThreadGroupStartIsIgnored)
{
	RUDPThreadManager manager;
	std::atomic_int firstGroupStarted{};
	std::atomic_int duplicateGroupStarted{};

	manager.StartThreads(THREAD_GROUP::RECV_LOGIC_WORKER_THREAD, [&](const std::stop_token& stopToken, unsigned char)
	{
		firstGroupStarted.fetch_add(1, std::memory_order_relaxed);
		while (not stopToken.stop_requested())
		{
			Sleep(1);
		}
	}, 1);

	ASSERT_TRUE(WaitUntil(1s, [&]() { return firstGroupStarted.load(std::memory_order_relaxed) == 1; }));

	manager.StartThreads(THREAD_GROUP::RECV_LOGIC_WORKER_THREAD, [&](const std::stop_token&, unsigned char)
	{
		duplicateGroupStarted.fetch_add(1, std::memory_order_relaxed);
	}, 1);

	Sleep(20);
	EXPECT_EQ(duplicateGroupStarted.load(std::memory_order_relaxed), 0);

	manager.StopThreadGroup(THREAD_GROUP::RECV_LOGIC_WORKER_THREAD);
}

TEST(RUDPThreadManagerTest, StopAllThreadsRequestsStopForEveryGroup)
{
	RUDPThreadManager manager;
	std::atomic_int startedCount{};
	std::atomic_int stoppedCount{};

	const auto worker = [&](const std::stop_token& stopToken, unsigned char)
	{
		startedCount.fetch_add(1, std::memory_order_relaxed);
		while (not stopToken.stop_requested())
		{
			Sleep(1);
		}
		stoppedCount.fetch_add(1, std::memory_order_relaxed);
	};

	manager.StartThreads(THREAD_GROUP::RETRANSMISSION_THREAD, worker, 2);
	manager.StartThreads(THREAD_GROUP::HEARTBEAT_THREAD, worker, 1);

	ASSERT_TRUE(WaitUntil(1s, [&]() { return startedCount.load(std::memory_order_relaxed) == 3; }));

	manager.StopAllThreads();

	EXPECT_EQ(stoppedCount.load(std::memory_order_relaxed), 3);
}

// ------------------------------------------------------------
// 스레드 수가 0인 그룹의 시작과 종료가 작업 실행이나 예외 없이 안전하게 완료되는지 확인합니다.
// ------------------------------------------------------------
TEST(RUDPThreadManagerTest, ZeroThreadGroupCanBeStartedAndStopped)
{
	RUDPThreadManager manager;
	std::atomic_int callCount{};

	manager.StartThreads(THREAD_GROUP::IO_WORKER_THREAD, [&](std::stop_token, unsigned char)
	{
		++callCount;
	}, 0);
	manager.StopThreadGroup(THREAD_GROUP::IO_WORKER_THREAD);

	EXPECT_EQ(callCount.load(), 0);
}

// ------------------------------------------------------------
// 존재하지 않거나 이미 종료된 그룹에 대한 종료 요청이 여러 번 호출되어도 안전한지 확인합니다.
// ------------------------------------------------------------
TEST(RUDPThreadManagerTest, StopOperationsAreIdempotentForMissingGroups)
{
	RUDPThreadManager manager;

	EXPECT_NO_FATAL_FAILURE(manager.StopThreadGroup(THREAD_GROUP::HEARTBEAT_THREAD));
	EXPECT_NO_FATAL_FAILURE(manager.StopAllThreads());
	EXPECT_NO_FATAL_FAILURE(manager.StopAllThreads());
}

// ------------------------------------------------------------
// 종료된 스레드 그룹을 같은 종류로 다시 시작하고 정상적으로 중지할 수 있는지 확인합니다.
// ------------------------------------------------------------
TEST(RUDPThreadManagerTest, StoppedGroupCanBeRestarted)
{
	RUDPThreadManager manager;
	std::latch firstStarted(1);
	std::latch firstStopped(1);
	std::binary_semaphore firstStopSignal(0);
	manager.StartThreads(THREAD_GROUP::RETRANSMISSION_THREAD,
		[&](const std::stop_token& stopToken, unsigned char)
		{
			std::stop_callback callback(stopToken, [&]() { firstStopSignal.release(); });
			firstStarted.count_down();
			firstStopSignal.acquire();
			firstStopped.count_down();
		}, 1);
	firstStarted.wait();
	manager.StopThreadGroup(THREAD_GROUP::RETRANSMISSION_THREAD);
	firstStopped.wait();

	std::latch secondStarted(1);
	std::binary_semaphore secondStopSignal(0);
	manager.StartThreads(THREAD_GROUP::RETRANSMISSION_THREAD,
		[&](const std::stop_token& stopToken, unsigned char)
		{
			std::stop_callback callback(stopToken, [&]() { secondStopSignal.release(); });
			secondStarted.count_down();
			secondStopSignal.acquire();
		}, 1);
	secondStarted.wait();

	manager.StopThreadGroup(THREAD_GROUP::RETRANSMISSION_THREAD);
}
