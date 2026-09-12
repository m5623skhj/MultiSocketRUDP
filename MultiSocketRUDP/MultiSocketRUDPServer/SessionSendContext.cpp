#include "PreCompile.h"
#include "SessionSendContext.h"
#include "SendPacketInfo.h"
#include <ranges>

SessionSendContext::SessionSendContext()
{
	ZeroMemory(rioSendBuffer, sizeof(rioSendBuffer));
}

bool SessionSendContext::Initialize(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable, unsigned short pendingQueueCapacity)
{
	const RIO_BUFFERID bufferId = rioFunctionTable.RIORegisterBuffer(rioSendBuffer, MAX_SEND_BUFFER_SIZE);
	if (bufferId == RIO_INVALID_BUFFERID)
	{
		return false;
	}

	sendBufferId = bufferId;
	pendingPacketQueue.Resize(pendingQueueCapacity);
	cachedSequenceSet.clear();

	return true;
}

void SessionSendContext::Cleanup(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable)
{
	if (sendBufferId != RIO_INVALID_BUFFERID)
	{
		rioFunctionTable.RIODeregisterBuffer(sendBufferId);
		sendBufferId = RIO_INVALID_BUFFERID;
	}
}

void SessionSendContext::Reset()
{
	ioMode.store(IO_MODE::IO_NONE_SENDING, std::memory_order_seq_cst);
	lastSendPacketSequence = 0;
	sendBufferId = RIO_INVALID_BUFFERID;

	{
		std::scoped_lock lock(sendPacketInfoQueueLock);

		if (reservedSendPacketInfo != nullptr)
		{
			SendPacketInfo::Free(reservedSendPacketInfo);
			reservedSendPacketInfo = nullptr;
		}

		while (not sendPacketInfoQueue.empty())
		{
			SendPacketInfo::Free(sendPacketInfoQueue.front());
			sendPacketInfoQueue.pop();
		}
		for (auto* info : unreliableQueue) SendPacketInfo::Free(info);
		unreliableQueue.clear();
		preferUnreliable = false;
	}

	{
		std::scoped_lock lock(pendingPacketQueueLock);
		ClearPendingQueue();
	}
}

bool SessionSendContext::IsSendPacketInfoQueueEmpty()
{
	std::scoped_lock lock(sendPacketInfoQueueLock);
	return sendPacketInfoQueue.empty() && unreliableQueue.empty();
}

size_t SessionSendContext::GetSendPacketInfoQueueSize()
{
	std::scoped_lock lock(sendPacketInfoQueueLock);
	return sendPacketInfoQueue.size() + unreliableQueue.size();
}

void SessionSendContext::PushSendPacketInfo(SendPacketInfo* info)
{
	std::scoped_lock lock(sendPacketInfoQueueLock);
	if (info->isUnreliable)
	{
		// The reserved packet is older than every queued packet and has not been copied yet.
		const bool hasReserved = reservedSendPacketInfo != nullptr && reservedSendPacketInfo->isUnreliable;
		if (unreliableQueue.size() + (hasReserved ? 1 : 0) >= unreliableQueueCapacity)
		{
			if (hasReserved)
			{
				SendPacketInfo::Free(reservedSendPacketInfo);
				reservedSendPacketInfo = nullptr;
			}
			else if (not unreliableQueue.empty())
			{
				SendPacketInfo::Free(unreliableQueue.front());
				unreliableQueue.pop_front();
			}
		}
		unreliableQueue.push_back(info);
		return;
	}
	sendPacketInfoQueue.push(info);
}

SendPacketInfo* SessionSendContext::TryGetFrontAndPop()
{
	std::scoped_lock lock(sendPacketInfoQueueLock);
	if (not unreliableQueue.empty() && (preferUnreliable || sendPacketInfoQueue.empty()))
	{
		auto* info = unreliableQueue.front();
		unreliableQueue.pop_front();
		preferUnreliable = false;
		return info;
	}
	if (sendPacketInfoQueue.empty())
	{
		return nullptr;
	}

	auto* front = sendPacketInfoQueue.front();
	sendPacketInfoQueue.pop();
	preferUnreliable = true;

	return front;
}

bool SessionSendContext::IsNothingToSend()
{
	std::scoped_lock lock(sendPacketInfoQueueLock);
	return sendPacketInfoQueue.empty() && unreliableQueue.empty() && reservedSendPacketInfo == nullptr;
}

SendPacketInfo* SessionSendContext::GetReservedSendPacketInfo()
{
	std::scoped_lock lock(sendPacketInfoQueueLock);
	return reservedSendPacketInfo;
}
SendPacketInfo* SessionSendContext::TakeReservedSendPacketInfo()
{
	std::scoped_lock lock(sendPacketInfoQueueLock);
	SendPacketInfo* info = reservedSendPacketInfo;
	reservedSendPacketInfo = nullptr;
	return info;
}
void SessionSendContext::SetReservedSendPacketInfo(SendPacketInfo* info)
{
	std::scoped_lock lock(sendPacketInfoQueueLock);
	// Producers may have filled the queue while this packet was being assembled.
	if (info->isUnreliable && unreliableQueue.size() >= unreliableQueueCapacity)
	{
		SendPacketInfo::Free(info);
		return;
	}
	reservedSendPacketInfo = info;
}

char* SessionSendContext::GetRIOSendBuffer()
{
	return rioSendBuffer;
}

RIO_BUFFERID SessionSendContext::GetSendBufferId() const
{
	return sendBufferId;
}

void SessionSendContext::SetSendRIOBufferId(const RIO_BUFFERID id)
{
	sendBufferId = id;
}

std::atomic<IO_MODE>& SessionSendContext::GetIOMode()
{
	return ioMode;
}

void SessionSendContext::InsertSendPacketInfo(const PacketSequence sequence, SendPacketInfo* info)
{
	std::unique_lock lock(sendPacketInfoMapLock);
	const auto [_, inserted] = sendPacketInfoMap.try_emplace(sequence, info);
	if (inserted)
	{
		info->AddRefCount();
	}
}

SendPacketInfo* SessionSendContext::FindSendPacketInfo(const PacketSequence sequence)
{
	std::shared_lock lock(sendPacketInfoMapLock);
	const auto itor = sendPacketInfoMap.find(sequence);
	return itor != sendPacketInfoMap.end() ? itor->second : nullptr;
}

void SessionSendContext::EraseSendPacketInfo(const PacketSequence sequence)
{
	std::unique_lock lock(sendPacketInfoMapLock);
	const auto itor = sendPacketInfoMap.find(sequence);
	if (itor != sendPacketInfoMap.end())
	{
		SendPacketInfo::Free(itor->second);
		sendPacketInfoMap.erase(itor);
	}
}

SendPacketInfo* SessionSendContext::FindAndEraseSendPacketInfo(const PacketSequence sequence)
{
	std::unique_lock lock(sendPacketInfoMapLock);
	const auto itor = sendPacketInfoMap.find(sequence);
	if (itor == sendPacketInfoMap.end())
	{
		return nullptr;
	}

	SendPacketInfo* info = itor->second;
	sendPacketInfoMap.erase(itor);

	return info;
}

void SessionSendContext::ForEachAndClearSendPacketInfoMap(const std::function<void(SendPacketInfo*)>& func)
{
	std::unique_lock lock(sendPacketInfoMapLock);
	for (const auto& item : sendPacketInfoMap | std::views::values)
	{
		func(item);
	}

	sendPacketInfoMap.clear();
}

std::set<MultiSocketRUDP::PacketSequenceSetKey>& SessionSendContext::GetCachedSequenceSet()
{
	return cachedSequenceSet;
}

PacketSequence SessionSendContext::GetLastSendPacketSequence() const
{
	return lastSendPacketSequence.load();
}

PacketSequence SessionSendContext::IncrementLastSendPacketSequence()
{
	return ++lastSendPacketSequence;
}

void SessionSendContext::InitializePendingQueue(const unsigned short capacity)
{
	pendingPacketQueue.Resize(capacity);
}

std::mutex& SessionSendContext::GetPendingQueueLock()
{
	return pendingPacketQueueLock;
}

bool SessionSendContext::IsPendingQueueEmpty() const noexcept
{
	return pendingPacketQueue.IsEmpty();
}

bool SessionSendContext::IsPendingQueueFull() const noexcept
{
	return pendingPacketQueue.IsFull();
}

const std::pair<PacketSequence, NetBuffer*>& SessionSendContext::PendingQueueFront() const
{
	return pendingPacketQueue.Front();
}

bool SessionSendContext::PushToPendingQueue(const PacketSequence sequence, NetBuffer* buffer)
{
	return pendingPacketQueue.Push({ sequence, buffer });
}

bool SessionSendContext::PopFromPendingQueue(std::pair<PacketSequence, NetBuffer*>& item)
{
	return pendingPacketQueue.Pop(item);
}

void SessionSendContext::ClearPendingQueue()
{
	std::pair<PacketSequence, NetBuffer*> item;
	while (pendingPacketQueue.Pop(item))
	{
		NetBuffer::Free(item.second);
	}
}
