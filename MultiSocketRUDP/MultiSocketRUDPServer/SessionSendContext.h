#pragma once
#include <queue>
#include <map>
#include <set>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <functional>
#include <MSWSock.h>
#include "PacketSequenceSetKey.h"

struct SendPacketInfo;

enum class IO_MODE : LONG
{
	IO_NONE_SENDING = 0
	, IO_SENDING
};

class SessionSendContext
{
public:
	SessionSendContext();
	~SessionSendContext() = default;

	SessionSendContext(const SessionSendContext&) = delete;
	SessionSendContext(SessionSendContext&&) = delete;
	SessionSendContext& operator=(const SessionSendContext&) = delete;
	SessionSendContext& operator=(SessionSendContext&&) = delete;

public:
	[[nodiscard]]
	bool IsSendPacketInfoQueueEmpty();
	[[nodiscard]]
	size_t GetSendPacketInfoQueueSize();
	void PushSendPacketInfo(SendPacketInfo* info);
	[[nodiscard]]
	SendPacketInfo* TryGetFrontAndPop();
	[[nodiscard]]
	bool IsNothingToSend();

	[[nodiscard]]
	SendPacketInfo* GetReservedSendPacketInfo() const;
	void SetReservedSendPacketInfo(SendPacketInfo* info);

	[[nodiscard]]
	char* GetRIOSendBuffer();
	[[nodiscard]]
	RIO_BUFFERID GetSendBufferId() const;
	void SetSendRIOBufferId(RIO_BUFFERID id);

	[[nodiscard]]
	IO_MODE& GetIOMode();

	void InsertSendPacketInfo(PacketSequence sequence, SendPacketInfo* info);

	[[nodiscard]]
	SendPacketInfo* FindSendPacketInfo(PacketSequence sequence);

	void EraseSendPacketInfo(PacketSequence sequence);
	[[nodiscard]]
	SendPacketInfo* FindAndEraseSendPacketInfo(PacketSequence sequence);
	void ForEachAndClearSendPacketInfoMap(const std::function<void(SendPacketInfo*)>& func);

	[[nodiscard]]
	std::set<MultiSocketRUDP::PacketSequenceSetKey>& GetCachedSequenceSet();
	[[nodiscard]]
	std::mutex& GetCachedSequenceSetLock();

	[[nodiscard]]
	PacketSequence GetLastSendPacketSequence() const;
	[[nodiscard]]
	PacketSequence IncrementLastSendPacketSequence();

	void Reset();

private:
	SendPacketInfo* reservedSendPacketInfo = nullptr;
	char rioSendBuffer[MAX_SEND_BUFFER_SIZE]{};
	RIO_BUFFERID sendBufferId = RIO_INVALID_BUFFERID;
	IO_MODE ioMode = IO_MODE::IO_NONE_SENDING;

	std::mutex sendPacketInfoQueueLock;
	std::queue<SendPacketInfo*> sendPacketInfoQueue;

	std::atomic<PacketSequence> lastSendPacketSequence{};
	std::map<PacketSequence, SendPacketInfo*> sendPacketInfoMap;
	std::shared_mutex sendPacketInfoMapLock;

	std::set<MultiSocketRUDP::PacketSequenceSetKey> cachedSequenceSet;
	std::mutex cachedSequenceSetLock;
};