#pragma once
#include <thread>

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
	};

public:
	void Start();
	void Stop();

public:
	[[nodiscard]]
	bool IsRunning() const { return isRunning; }
	[[nodiscard]]
	uint64_t GetTickCount() const { return tickCounter.tickCount.load(std::memory_order_relaxed); }
	void SetTickInterval(const int intervalMs) { tickInterval = intervalMs; }

private:
	void UpdateTick();

private:
	TickCounter tickCounter;

	bool isRunning = false;
	int tickInterval = 16;

	std::jthread tickerThread;
};