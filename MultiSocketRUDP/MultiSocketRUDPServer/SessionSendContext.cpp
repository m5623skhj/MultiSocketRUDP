#include "PreCompile.h"
#include "SessionSendContext.h"
#include "SendPacketInfo.h"
#include <ranges>

SessionSendContext::SessionSendContext()
{
	ZeroMemory(rioSendBuffer, sizeof(rioSendBuffer));
}

bool SessionSendContext::Initialize(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable)
{
	const RIO_BUFFERID bufferId = rioFunctionTable.RIORegisterBuffer(rioSendBuffer, MAX_SEND_BUFFER_SIZE);
	if (bufferId == RIO_INVALID_BUFFERID)
	{
		return false;
	}

	sendBufferId = bufferId;
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
	lastSendPacketSequence = 0;
	sendBufferId = RIO_INVALID_BUFFERID;

	if (reservedSendPacketInfo != nullptr)
	{
		SendPacketInfo::Free(reservedSendPacketInfo);
		reservedSendPacketInfo = nullptr;
	}

	std::scoped_lock lock(sendPacketInfoQueueLock);
	while (not sendPacketInfoQueue.empty())
	{
		SendPacketInfo::Free(sendPacketInfoQueue.front());
		sendPacketInfoQueue.pop();
	}
}

bool SessionSendContext::IsSendPacketInfoQueueEmpty()
{
	std::scoped_lock lock(sendPacketInfoQueueLock);
	return sendPacketInfoQueue.empty();
}

size_t SessionSendContext::GetSendPacketInfoQueueSize()
{
	std::scoped_lock lock(sendPacketInfoQueueLock);
	return sendPacketInfoQueue.size();
}

void SessionSendContext::PushSendPacketInfo(SendPacketInfo* info)
{
	std::scoped_lock lock(sendPacketInfoQueueLock);
	sendPacketInfoQueue.push(info);
}

SendPacketInfo* SessionSendContext::TryGetFrontAndPop()
{
	std::scoped_lock lock(sendPacketInfoQueueLock);
	if (sendPacketInfoQueue.empty())
	{
		return nullptr;
	}

	auto* front = sendPacketInfoQueue.front();
	sendPacketInfoQueue.pop();

	return front;
}

bool SessionSendContext::IsNothingToSend()
{
	std::scoped_lock lock(sendPacketInfoQueueLock);
	return sendPacketInfoQueue.empty() && reservedSendPacketInfo == nullptr;
}

SendPacketInfo* SessionSendContext::GetReservedSendPacketInfo() const
{
	return reservedSendPacketInfo;
}

void SessionSendContext::SetReservedSendPacketInfo(SendPacketInfo* info)
{
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

IO_MODE& SessionSendContext::GetIOMode()
{
	return ioMode;
}

void SessionSendContext::InsertSendPacketInfo(const PacketSequence sequence, SendPacketInfo* info)
{
	std::unique_lock lock(sendPacketInfoMapLock);
	sendPacketInfoMap.insert({ sequence, info });
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
	sendPacketInfoMap.erase(sequence);
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

std::mutex& SessionSendContext::GetCachedSequenceSetLock()
{
	return cachedSequenceSetLock;
}

PacketSequence SessionSendContext::GetLastSendPacketSequence() const
{
	return lastSendPacketSequence.load();
}

PacketSequence SessionSendContext::IncrementLastSendPacketSequence()
{
	return ++lastSendPacketSequence;
}
