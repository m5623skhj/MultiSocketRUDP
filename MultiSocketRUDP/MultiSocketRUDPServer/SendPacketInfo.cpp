#include "PreCompile.h"
#include "SendPacketInfo.h"

CTLSMemoryPool<SendPacketInfo>* sendPacketInfoPool = new CTLSMemoryPool<SendPacketInfo>(2, true);

SendPacketInfo::~SendPacketInfo()
{
	owner = {};
	retransmissionCount = {};
	sendPacketSequence = {};
	retransmissionTimeStamp = {};
	listItor = {};
	isErasedPacketInfo = {};
	isReplyType = {};
}

void SendPacketInfo::Initialize(RUDPSession* inOwner
	, NetBuffer* inBuffer
	, const PacketSequence inSendPacketSequence
	, const bool inIsReplyType)
{
	owner = inOwner;
	buffer = inBuffer;
	sendPacketSequence = inSendPacketSequence;
	isReplyType = inIsReplyType;

	retransmissionCount = {};
	retransmissionTimeStamp = {};
	isErasedPacketInfo = {};

	refCount = 1;
}

void SendPacketInfo::AddRefCount()
{
	refCount.fetch_add(1, std::memory_order_relaxed);
}

void SendPacketInfo::Free(SendPacketInfo* deleteTarget)
{
	if (deleteTarget == nullptr)
	{
		return;
	}

	if (deleteTarget->refCount.fetch_sub(1, std::memory_order_relaxed) == 1)
	{
		NetBuffer::Free(deleteTarget->buffer);
		sendPacketInfoPool->Free(deleteTarget);
	}
}

void SendPacketInfo::Free(SendPacketInfo* deleteTarget, const char subCount)
{
	if (deleteTarget == nullptr)
	{
		return;
	}

	if (deleteTarget->refCount.fetch_sub(subCount, std::memory_order_relaxed) == 1)
	{
		NetBuffer::Free(deleteTarget->buffer);
		sendPacketInfoPool->Free(deleteTarget);
	}
}

NetBuffer* SendPacketInfo::GetBuffer() const
{
	return buffer;
}
