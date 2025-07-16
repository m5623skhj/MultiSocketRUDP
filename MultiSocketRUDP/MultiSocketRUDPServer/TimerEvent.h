#pragma once
#include <functional>

using TimerEventInterval = unsigned int;
using TimerEventHandler = std::function<void()>;
using TimerEventId = unsigned short;

class Ticker;

class TimerEvent : public std::enable_shared_from_this<TimerEvent>
{
	friend Ticker;

public:
	TimerEvent() = delete;
	virtual ~TimerEvent() = default;

public:
	[[nodiscard]]
	TimerEventId GetTimerEventId() const { return timerEventId; }

	[[nodiscard]]
	bool ShouldFire(const uint64_t currentTick) const
	{
		return nextTick <= currentTick;
	}

protected:
	explicit TimerEvent(const TimerEventId inTimerEventId, const TimerEventInterval inIntervalMs);

private:
	virtual void Fire() = 0;
	void SetNextTick(const uint64_t nowTickMs);

private:
	TimerEventId timerEventId;
	TimerEventInterval intervalMs;

	uint64_t nextTick{};
};

class TimerEventCreator
{
public:
	template<typename TimerEventObjectType, typename... Args>
	static std::shared_ptr<TimerEventObjectType> Create(const TimerEventInterval inIntervalMs, Args&&... args)
	{
		static_assert(std::is_base_of_v<TimerEvent, TimerEventObjectType>, "TimerEventObjectType must inherit from TimerEvent");

		TimerEventId newId = timerEventIdGenerator.fetch_add(1, std::memory_order_relaxed);
		return std::make_shared<TimerEventObjectType>(newId, inIntervalMs, std::forward<Args>(args)...);
	}

private:
	static inline std::atomic<TimerEventId> timerEventIdGenerator{ 1 };
};