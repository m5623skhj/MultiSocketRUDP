#pragma once
#include <atomic>
#include <cstdint>
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

	SendPacketInfo() = default;
	~SendPacketInfo();

	void Initialize(RUDPSession* inOwner, uint32_t inOwnerGeneartion, NetBuffer* inBuffer, PacketSequence inSendPacketSequence, bool inIsReplyType);
	[[nodiscard]]
	bool IsOwnerValid() const;

	void AddRefCount();

	static void Free(SendPacketInfo* deleteTarget);

	[[nodiscard]]
	NetBuffer* GetBuffer() const;
};
