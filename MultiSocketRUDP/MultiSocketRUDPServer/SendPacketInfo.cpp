#include "PreCompile.h"
#include "SendPacketInfo.h"
#include "RUDPSession.h"
#include "Logger.h"
#include "LogExtension.h"

CTLSMemoryPool<SendPacketInfo>* sendPacketInfoPool = new CTLSMemoryPool<SendPacketInfo>(2, true);

SendPacketInfo::~SendPacketInfo()
{
	owner = {};
	ownerGeneration = {};
	retransmissionCount = {};
	sendPacketSequence = {};
	scheduleVersion = {};
	isErasedPacketInfo = {};
	buffer = {};
	isReplyType = {};
}

void SendPacketInfo::Initialize(RUDPSession* inOwner
	, const uint32_t inOwnerGeneration
	, NetBuffer* inBuffer
	, const PacketSequence inSendPacketSequence
	, const bool inIsReplyType)
{
	owner = inOwner;
	ownerGeneration = inOwnerGeneration;
	buffer = inBuffer;
	sendPacketSequence = inSendPacketSequence;
	isReplyType = inIsReplyType;

	retransmissionCount = {};
	scheduleVersion = {};
	isErasedPacketInfo = {};

	refCount.store(1, std::memory_order_release);
}

bool SendPacketInfo::IsOwnerValid() const
{
	return owner != nullptr && owner->GetSessionGeneration() == ownerGeneration;
}

void SendPacketInfo::AddRefCount()
{
	const int32_t prev = refCount.fetch_add(1, std::memory_order_relaxed);
	if (prev <= 0)
	{
		LOG_ERROR(std::format("SendPacketInfo refCount is invalid before AddRefCount. prev is {}", prev));
	}
}

void SendPacketInfo::Free(SendPacketInfo* deleteTarget)
{
	if (deleteTarget == nullptr)
	{
		return;
	}

	const int32_t prev = deleteTarget->refCount.fetch_sub(1, std::memory_order_acq_rel);
	if (prev <= 0)
	{
		LOG_ERROR(std::format("SendPacketInfo refCount is invalid prev is {}", prev));
		return;
	}

	if (prev == 1)
	{
		NetBuffer::Free(deleteTarget->buffer);
		sendPacketInfoPool->Free(deleteTarget);
	}
}

NetBuffer* SendPacketInfo::GetBuffer() const
{
	return buffer;
}
