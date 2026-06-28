#pragma once

#include <Windows.h>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <queue>
#include <vector>

#include "SendPacketInfo.h"

struct RetransmissionHeapEntry
{
	std::chrono::steady_clock::time_point deadline{};
	uint64_t version{};
	SendPacketInfo* info{};
};

struct RetransmissionHeapEntryGreater
{
	bool operator()(const RetransmissionHeapEntry& lhs, const RetransmissionHeapEntry& rhs) const noexcept
	{
		return lhs.deadline > rhs.deadline;
	}
};

struct RetransmissionScheduler
{
	std::mutex lock;
	std::priority_queue<RetransmissionHeapEntry, std::vector<RetransmissionHeapEntry>, RetransmissionHeapEntryGreater> heap;
	HANDLE timerHandle{};
	HANDLE wakeEventHandle{};
};

inline void PushRetransmissionSchedule(
	OUT RetransmissionScheduler& scheduler,
	OUT SendPacketInfo& sendPacketInfo,
	const std::chrono::steady_clock::time_point deadline)
{
	++sendPacketInfo.scheduleVersion;
	sendPacketInfo.AddRefCount();
	scheduler.heap.push(RetransmissionHeapEntry{ deadline, sendPacketInfo.scheduleVersion, &sendPacketInfo });
}

[[nodiscard]]
inline bool SignalRetransmissionWakeEvent(const RetransmissionScheduler& scheduler)
{
	return scheduler.wakeEventHandle == NULL || SetEvent(scheduler.wakeEventHandle) != FALSE;
}
