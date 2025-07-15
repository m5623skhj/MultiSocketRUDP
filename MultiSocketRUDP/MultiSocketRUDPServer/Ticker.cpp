#include "PreCompile.h"
#include "Ticker.h"

void Ticker::Start()
{
	tickerThread = std::jthread([this]() { UpdateTick(); });
}

void Ticker::Stop()
{
	isRunning = false;
	tickerThread.join();
}

void Ticker::UpdateTick()
{
	isRunning = true;

	while (isRunning)
	{
		tickCounter.tickCount.fetch_add(1, std::memory_order_relaxed);
		std::this_thread::sleep_for(std::chrono::milliseconds(tickInterval));
	}
}
