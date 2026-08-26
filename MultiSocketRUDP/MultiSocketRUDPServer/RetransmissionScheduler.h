#pragma once

#include <Windows.h>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <queue>
#include <vector>

#include "SendPacketInfo.h"

// ----------------------------------------
// @brief 재전송 마감 시각과 해당 시점의 패킷 스케줄 버전을 보관하는 힙 항목입니다.
// version이 SendPacketInfo의 현재 버전과 다르면 이전 재예약에서 남은 stale 항목입니다.
// ----------------------------------------
struct RetransmissionHeapEntry
{
	std::chrono::steady_clock::time_point deadline{};
	uint64_t version{};
	SendPacketInfo* info{};
};

// ----------------------------------------
// @brief 가장 이른 마감 시각이 힙의 top에 오도록 하는 비교 함수입니다.
// ----------------------------------------
struct RetransmissionHeapEntryGreater
{
	bool operator()(const RetransmissionHeapEntry& lhs, const RetransmissionHeapEntry& rhs) const noexcept
	{
		return lhs.deadline > rhs.deadline;
	}
};

// ----------------------------------------
// @brief 워커 스레드별 재전송 힙과 대기용 타이머·깨우기 이벤트를 묶어 관리합니다.
// heap과 SendPacketInfo::scheduleVersion 접근은 lock을 보유한 상태에서 수행해야 합니다.
// ----------------------------------------
struct RetransmissionScheduler
{
	std::mutex lock;
	std::priority_queue<RetransmissionHeapEntry, std::vector<RetransmissionHeapEntry>, RetransmissionHeapEntryGreater> heap;
	HANDLE timerHandle{};
	HANDLE wakeEventHandle{};
};

// ----------------------------------------
// @brief 패킷의 스케줄 버전과 참조 카운트를 증가시킨 뒤 새 마감 시각을 힙에 추가합니다.
// 호출자는 scheduler.lock을 보유해야 하며, 힙에서 항목을 제거한 경로가 추가된 참조를 해제해야 합니다.
// ----------------------------------------
inline void PushRetransmissionSchedule(
	OUT RetransmissionScheduler& scheduler,
	OUT SendPacketInfo& sendPacketInfo,
	const std::chrono::steady_clock::time_point deadline)
{
	++sendPacketInfo.scheduleVersion;
	sendPacketInfo.AddRefCount();
	scheduler.heap.push(RetransmissionHeapEntry{ deadline, sendPacketInfo.scheduleVersion, &sendPacketInfo });
}

// ----------------------------------------
// @brief 재전송 워커가 변경된 최단 마감 시각을 다시 계산하도록 깨우기 이벤트를 신호합니다.
// ----------------------------------------
[[nodiscard]]
inline bool SignalRetransmissionWakeEvent(const RetransmissionScheduler& scheduler)
{
	return SetEvent(scheduler.wakeEventHandle);
}
