#include "PreCompile.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include "../MultiSocketRUDPServer/Ticker.h"
#include "../MultiSocketRUDPServer/TimerEvent.h"

class MockTimerEvent : public TimerEvent
{
public:
    MockTimerEvent(const TimerEventId id, const TimerEventInterval intervalMs)
        : TimerEvent(id, intervalMs)
    {
    }

    std::atomic<int> fireCount{ 0 };

private:
    void Fire() override
    {
        ++fireCount;
    }
};

class TickerTimerEventTest : public::testing::Test
{
protected:
	void SetUp() override
	{
        Ticker::GetInstance().Start(TICK_INTERVAL_MS);
	}

	void TearDown() override
	{
		Ticker::GetInstance().Stop();
	}

    static bool WaitUntilFireCount(const MockTimerEvent& event, const int targetCount, const int timeoutMs = WAIT_TIMEOUT_MS)
	{
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (event.fireCount.load() >= targetCount)
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        return false;
	}

    static constexpr unsigned int TICK_INTERVAL_MS = 5;
    static constexpr int WAIT_TIMEOUT_MS = 1000;
};

// ------------------------------------------------------------
// 등록한 이벤트는 interval 경과 후 Fire되어야 한다
// ------------------------------------------------------------
TEST_F(TickerTimerEventTest, RegisterTimerEvent_FiresAfterInterval)
{
    constexpr TimerEventInterval intervalMs = 10;

    const auto event = TimerEventCreator::Create<MockTimerEvent>(intervalMs);
    ASSERT_TRUE(Ticker::GetInstance().RegisterTimerEvent(event));

    EXPECT_TRUE(WaitUntilFireCount(*event, 1));
}

// ------------------------------------------------------------
// 등록한 이벤트는 interval마다 반복적으로 Fire되어야 한다
// ------------------------------------------------------------
TEST_F(TickerTimerEventTest, RegisterTimerEvent_FiresRepeatedly)
{
    constexpr TimerEventInterval intervalMs = 10;
    constexpr int targetFireCount = 3;

    const auto event = TimerEventCreator::Create<MockTimerEvent>(intervalMs);
    ASSERT_TRUE(Ticker::GetInstance().RegisterTimerEvent(event));

    EXPECT_TRUE(WaitUntilFireCount(*event, targetFireCount));
}

// ------------------------------------------------------------
// 서로 다른 interval을 가진 이벤트들이 각자의 주기로 Fire되어야 한다
// ------------------------------------------------------------
TEST_F(TickerTimerEventTest, MultipleEvents_FireAtTheirOwnInterval)
{
    constexpr TimerEventInterval fastIntervalMs = 10;
    constexpr TimerEventInterval slowIntervalMs = 50;

    const auto fastEvent = TimerEventCreator::Create<MockTimerEvent>(fastIntervalMs);
    const auto slowEvent = TimerEventCreator::Create<MockTimerEvent>(slowIntervalMs);

    ASSERT_TRUE(Ticker::GetInstance().RegisterTimerEvent(fastEvent));
    ASSERT_TRUE(Ticker::GetInstance().RegisterTimerEvent(slowEvent));

    ASSERT_TRUE(WaitUntilFireCount(*slowEvent, 2));

    EXPECT_GT(fastEvent->fireCount.load(), slowEvent->fireCount.load());
}

// ------------------------------------------------------------
// nullptr 이벤트 등록 시 false를 반환해야 한다
// ------------------------------------------------------------
TEST_F(TickerTimerEventTest, RegisterTimerEvent_ReturnsFalse_WhenNullptr)
{
    EXPECT_FALSE(Ticker::GetInstance().RegisterTimerEvent(nullptr));
}

// ------------------------------------------------------------
// 해제된 이벤트는 이후 Fire되지 않아야 한다
// ------------------------------------------------------------
TEST_F(TickerTimerEventTest, UnregisterTimerEvent_StopsFiring)
{
    constexpr TimerEventInterval intervalMs = 10;

    const auto event = TimerEventCreator::Create<MockTimerEvent>(intervalMs);
    ASSERT_TRUE(Ticker::GetInstance().RegisterTimerEvent(event));

    ASSERT_TRUE(WaitUntilFireCount(*event, 1));

    Ticker::GetInstance().UnregisterTimerEvent(event->GetTimerEventId());

    const int countAfterUnregister = event->fireCount.load();

    ASSERT_TRUE(WaitUntilFireCount(*event, countAfterUnregister + 2, 200)
        == false || event->fireCount.load() <= countAfterUnregister + 1);
}

// ------------------------------------------------------------
// 등록하지 않은 id를 해제해도 크래시 없이 안전하게 처리되어야 한다
// ------------------------------------------------------------
TEST_F(TickerTimerEventTest, UnregisterTimerEvent_InvalidId_DoesNotCrash)
{
    constexpr TimerEventId invalidId = 9999;
    EXPECT_NO_FATAL_FAILURE(Ticker::GetInstance().UnregisterTimerEvent(invalidId));
}

// ------------------------------------------------------------
// Stop 후에는 이벤트가 Fire되지 않아야 한다
// ------------------------------------------------------------
TEST_F(TickerTimerEventTest, Stop_PreventsFiring)
{
    constexpr TimerEventInterval intervalMs = 10;

    const auto event = TimerEventCreator::Create<MockTimerEvent>(intervalMs);
    ASSERT_TRUE(Ticker::GetInstance().RegisterTimerEvent(event));

    ASSERT_TRUE(WaitUntilFireCount(*event, 1));

    Ticker::GetInstance().Stop();
    const int countAfterStop = event->fireCount.load();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(event->fireCount.load(), countAfterStop);
}