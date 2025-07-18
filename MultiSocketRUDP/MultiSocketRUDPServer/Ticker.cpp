#include "PreCompile.h"
#include "Ticker.h"
#include <ranges>

using Clock = std::chrono::steady_clock;

void Ticker::Start(const unsigned int intervalMs)
{
	tickInterval = intervalMs;
	if (unregisterTargetList.capacity() == 0)
	{
		unregisterTargetList.reserve(32);
	}

	tickCounter.nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
	tickerThread = std::jthread([this]() { UpdateTick(); });
}

void Ticker::Stop()
{
	isRunning = false;
	tickerThread.join();

	{
		std::unique_lock lock(unregisterListMutex);
		unregisterTargetList.clear();
	}

	{
		std::unique_lock lock(timerEventsMutex);
		timerEvents.clear();
	}
}

void Ticker::UpdateTick()
{
	isRunning = true;

	while (isRunning)
	{
		tickCounter.nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
		UnregisterTimerEventImpl();
		{
			std::shared_lock lock(timerEventsMutex);
			for (const auto& timerEvent : timerEvents | std::views::values)
			{
				if (not timerEvent->ShouldFire(tickCounter.nowMs))
				{
					continue;
				}

				timerEvent->Fire();
				timerEvent->SetNextTick(tickCounter.nowMs);
			}
		}
		tickCounter.tickCount.fetch_add(1, std::memory_order_relaxed);

		std::this_thread::sleep_for(std::chrono::milliseconds(tickInterval));
	}
}

void Ticker::UnregisterTimerEventImpl()
{
	std::vector<TimerEventId> copyUnregisterTargetList;
	{
		std::scoped_lock lock(unregisterListMutex);
		if (unregisterTargetList.empty())
		{
			return;
		}

		copyUnregisterTargetList.swap(unregisterTargetList);
	}

	std::unique_lock lock(timerEventsMutex);
	for (TimerEventId id : copyUnregisterTargetList)
	{
		timerEvents.erase(id);
	}
}

bool Ticker::RegisterTimerEvent(const std::shared_ptr<TimerEvent>& eventObject)
{
	if (eventObject == nullptr)
	{
		return false;
	}

	eventObject->SetNextTick(tickCounter.nowMs + eventObject->intervalMs);
	{
		std::unique_lock lock(timerEventsMutex);
		timerEvents.emplace(eventObject->GetTimerEventId(), eventObject);
	}

	return true;
}

void Ticker::UnregisterTimerEvent(const TimerEventId timerEventId)
{
	std::scoped_lock lock(unregisterListMutex);
	if (std::ranges::find(unregisterTargetList, timerEventId) == unregisterTargetList.end())
	{
		unregisterTargetList.push_back(timerEventId);
	}
}