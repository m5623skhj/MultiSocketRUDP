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
	lastSendTime = {};
	canUseRttSample.store(false, std::memory_order_relaxed);
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
	lastSendTime = {};
	canUseRttSample.store(not inIsReplyType, std::memory_order_relaxed);

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

void SendPacketInfo::MarkSentForRttSample(const std::chrono::steady_clock::time_point now)
{
	if (isReplyType)
	{
		return;
	}

	std::scoped_lock lock(rttSampleLock);
	lastSendTime = now;
}

void SendPacketInfo::InvalidateRttSample()
{
	canUseRttSample.store(false, std::memory_order_relaxed);
}

bool SendPacketInfo::TryGetRttSample(
	const std::chrono::steady_clock::time_point now,
	OUT std::chrono::steady_clock::duration& outSample) const
{
	if (isReplyType || canUseRttSample.load(std::memory_order_relaxed) == false)
	{
		return false;
	}

	std::chrono::steady_clock::time_point capturedSendTime{};
	{
		std::scoped_lock lock(rttSampleLock);
		if (lastSendTime.time_since_epoch().count() == 0)
		{
			return false;
		}

		capturedSendTime = lastSendTime;
	}

	if (canUseRttSample.load(std::memory_order_relaxed) == false)
	{
		return false;
	}

	outSample = now - capturedSendTime;
	return outSample.count() > 0;
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
