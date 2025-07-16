#include "PreCompile.h"
#include "TimerEvent.h"

#include "LogExtension.h"
#include "Logger.h"

TimerEvent::TimerEvent(const TimerEventId inTimerEventId, const TimerEventInterval inIntervalMs)
	: timerEventId(inTimerEventId)
	, intervalMs(inIntervalMs)
{
}

void TimerEvent::SetNextTick(const uint64_t nowTickMs)
{
	nextTick = nowTickMs + intervalMs;
}