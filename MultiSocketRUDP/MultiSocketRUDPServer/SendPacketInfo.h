#pragma once
#include <list>
#include <atomic>
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
	unsigned long long retransmissionTimeStamp{};
	std::atomic_bool isErasedPacketInfo{};
	bool isInSendPacketInfoList{};
	bool isReplyType{};
	std::list<SendPacketInfo*>::iterator listItor;
	std::atomic_int8_t refCount{};

	SendPacketInfo() = default;
	~SendPacketInfo();

	void Initialize(RUDPSession* inOwner, uint32_t inOwnerGeneartion, NetBuffer* inBuffer, PacketSequence inSendPacketSequence, bool inIsReplyType);
	[[nodiscard]]
	bool IsOwnerValid() const;

	void AddRefCount();

	static void Free(SendPacketInfo* deleteTarget);
	static void Free(SendPacketInfo* deleteTarget, char subCount);

	[[nodiscard]]
	NetBuffer* GetBuffer() const;
};
