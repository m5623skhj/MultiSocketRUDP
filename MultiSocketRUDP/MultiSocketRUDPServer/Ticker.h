#pragma once
#include <thread>
#include <map>
#include <shared_mutex>
#include "TimerEvent.h"

class Ticker
{
private:
	Ticker() = default;
	~Ticker() = default;

public:
	static Ticker& GetInstance()
	{
		static Ticker instance;
		return instance;
	}
	Ticker(const Ticker&) = delete;
	Ticker& operator=(const Ticker&) = delete;
	Ticker(Ticker&&) = delete;
	Ticker& operator=(Ticker&&) = delete;

private:
	// for false sharing prevention
	struct alignas(std::hardware_destructive_interference_size) TickCounter
	{
		std::atomic<uint64_t> tickCount{ 0 };
		uint64_t nowMs{ 0 };
	};

public:
	void Start(unsigned int intervalMs = 16);
	void Stop();

public:
	[[nodiscard]]
	bool IsRunning() const { return isRunning; }
	[[nodiscard]]
	uint64_t GetTickCount() const { return tickCounter.tickCount.load(std::memory_order_relaxed); }
	[[nodiscard]]
	uint64_t GetNowMs() const { return tickCounter.nowMs; }
	[[nodiscard]]
	bool RegisterTimerEvent(const std::shared_ptr<TimerEvent>& eventObject);
	void UnregisterTimerEvent(TimerEventId timerEventId);

private:
	void UpdateTick();
	void UnregisterTimerEventImpl();

private:
	TickCounter tickCounter;

	bool isRunning = false;
	unsigned int tickInterval;

	std::jthread tickerThread;

	std::shared_mutex timerEventsMutex;
	std::map<TimerEventId, std::shared_ptr<TimerEvent>> timerEvents;

	std::mutex unregisterListMutex;
	std::vector<TimerEventId> unregisterTargetList;
};