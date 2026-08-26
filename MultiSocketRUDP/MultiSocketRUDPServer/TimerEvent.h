#pragma once
#include <functional>

using TimerEventInterval = unsigned int;
using TimerEventHandler = std::function<void()>;
using TimerEventId = unsigned short;

class Ticker;

// ----------------------------------------
// @brief Ticker가 일정 주기로 실행하는 타이머 이벤트의 추상 기반 클래스입니다.
// Ticker만 Fire와 다음 실행 시각 갱신을 호출하며 이벤트는 shared_ptr로 수명을 공유합니다.
// ----------------------------------------
class TimerEvent : public std::enable_shared_from_this<TimerEvent>
{
	friend Ticker;

public:
	TimerEvent() = delete;
	virtual ~TimerEvent() = default;

public:
	[[nodiscard]]
	TimerEventId GetTimerEventId() const { return timerEventId; }

	// ----------------------------------------
	// @brief 현재 tick이 예약된 다음 실행 tick에 도달했는지 확인합니다.
	// ----------------------------------------
	[[nodiscard]]
	bool ShouldFire(const uint64_t currentTick) const
	{
		return nextTick <= currentTick;
	}

protected:
	explicit TimerEvent(TimerEventId inTimerEventId, TimerEventInterval inIntervalMs);

private:
	virtual void Fire() = 0;
	void SetNextTick(uint64_t nowTickMs);

private:
	TimerEventId timerEventId;
	TimerEventInterval intervalMs;

	uint64_t nextTick{};
};

// ----------------------------------------
// @brief 전역 원자 ID를 부여하여 TimerEvent 파생 객체를 생성하는 팩토리입니다.
// ----------------------------------------
class TimerEventCreator
{
public:
	// ----------------------------------------
	// @brief 지정한 주기와 생성자 인자로 타이머 이벤트를 생성합니다.
	// @return Ticker에 등록할 수 있는 파생 이벤트 shared_ptr입니다.
	// ----------------------------------------
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
