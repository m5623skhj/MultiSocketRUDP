#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <NetServerSerializeBuffer.h>

#include "../Common/etc/CoreType.h"

class RUDPSession;

struct SendPacketInfo;
extern CTLSMemoryPool<SendPacketInfo>* sendPacketInfoPool;

struct SendPacketInfo
{
	NetBuffer* buffer{};
	RUDPSession* owner{};
	uint32_t ownerGeneration{};
	PacketRetransmissionCount retransmissionCount{};
	PacketSequence sendPacketSequence{};
	uint64_t scheduleVersion{};
	std::atomic_bool isErasedPacketInfo{};
	bool isReplyType{};
	std::atomic_int32_t refCount{};
	mutable std::mutex rttSampleLock;
	std::chrono::steady_clock::time_point lastSendTime{};
	std::atomic_bool canUseRttSample{};

	SendPacketInfo() = default;
	~SendPacketInfo();

	void Initialize(RUDPSession* inOwner, uint32_t inOwnerGeneartion, NetBuffer* inBuffer, PacketSequence inSendPacketSequence, bool inIsReplyType);
	[[nodiscard]]
	bool IsOwnerValid() const;

	void AddRefCount();
	// ----------------------------------------
	// @brief Records the latest send time for RTT sampling.
	// @param now Time when this packet is added to a send stream.
	// ----------------------------------------
	void MarkSentForRttSample(std::chrono::steady_clock::time_point now);
	// ----------------------------------------
	// @brief Disables RTT sampling for this packet after retransmission timeout.
	// ----------------------------------------
	void InvalidateRttSample();
	// ----------------------------------------
	// @brief Returns a valid RTT sample if this packet was never retransmitted.
	// @param now Time when ACK was received.
	// @param outSample Calculated RTT sample.
	// @return true when the sample can be used for RTO estimation.
	// ----------------------------------------
	[[nodiscard]]
	bool TryGetRttSample(std::chrono::steady_clock::time_point now, OUT std::chrono::steady_clock::duration& outSample) const;

	static void Free(SendPacketInfo* deleteTarget);

	[[nodiscard]]
	NetBuffer* GetBuffer() const;
};
