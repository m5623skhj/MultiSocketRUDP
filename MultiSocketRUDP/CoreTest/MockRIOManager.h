#pragma once
#include "IRIOManager.h"
#include <functional>
#include <optional>

class MockRIOManager final : public IRIOManager
{
public:
	[[nodiscard]]
	bool RIOReceiveEx(
		const RIO_RQ&, PRIO_BUF, DWORD,
		PRIO_BUF, PRIO_BUF, PRIO_BUF, PRIO_BUF,
		ULONG, PVOID) const override
	{
		++const_cast<MockRIOManager*>(this)->rioReceiveExCallCount;
		if (onRIOReceiveEx) return onRIOReceiveEx();
		return rioReceiveExReturn;
	}

	[[nodiscard]]
	bool RIOSendEx(
		const RIO_RQ& rioRQ, PRIO_BUF rioBuffer, DWORD,
		PRIO_BUF, PRIO_BUF remoteAddr, PRIO_BUF, PRIO_BUF,
		ULONG, PVOID requestContext) const override
	{
		auto* self = const_cast<MockRIOManager*>(this);
		++self->rioSendExCallCount;
		self->lastSendRequestQueue = rioRQ;
		self->lastSendRequestContext = requestContext;
		self->lastSendLength = rioBuffer != nullptr ? rioBuffer->Length : 0;
		self->lastSendBufferId = rioBuffer != nullptr ? rioBuffer->BufferId : RIO_INVALID_BUFFERID;
		self->lastSendRemoteAddress = remoteAddr;
		if (onRIOSendEx) return onRIOSendEx();
		return rioSendExReturn;
	}

	[[nodiscard]]
	RIO_BUFFERID RegisterRIOBuffer(char*, unsigned int) override
	{
		++registerRIOBufferCallCount;
		return registerRIOBufferReturn;
	}

	void DeregisterBuffer(RIO_BUFFERID) override
	{
		++deregisterBufferCallCount;
	}

	[[nodiscard]]
	unsigned long DequeueCompletions(ThreadIdType threadId, RIORESULT* results, ULONG maxResults) const override
	{
		++const_cast<MockRIOManager*>(this)->dequeueCompletionsCallCount;
		if (onDequeueCompletions) return onDequeueCompletions(threadId, results, maxResults);
		return dequeueCompletionsReturn;
	}

	[[nodiscard]]
	bool InitializeSessionRIO(RUDPSession&, ThreadIdType) const override
	{
		++const_cast<MockRIOManager*>(this)->initializeSessionRIOCallCount;
		return initializeSessionRIOReturn;
	}

	[[nodiscard]]
	const RIO_EXTENSION_FUNCTION_TABLE& GetRIOFunctionTable() const override
	{
		return dummyTable;
	}

	void ResetCounts()
	{
		rioReceiveExCallCount = 0;
		rioSendExCallCount = 0;
		lastSendRequestQueue = RIO_INVALID_RQ;
		lastSendRequestContext = nullptr;
		lastSendLength = 0;
		lastSendBufferId = RIO_INVALID_BUFFERID;
		lastSendRemoteAddress = nullptr;
		registerRIOBufferCallCount = 0;
		deregisterBufferCallCount = 0;
		dequeueCompletionsCallCount = 0;
		initializeSessionRIOCallCount = 0;
	}

	mutable RIO_EXTENSION_FUNCTION_TABLE dummyTable{};

	bool rioSendExReturn = true;
	int rioSendExCallCount = 0;
	std::function<bool()> onRIOSendEx;
	RIO_RQ lastSendRequestQueue = RIO_INVALID_RQ;
	PVOID lastSendRequestContext = nullptr;
	ULONG lastSendLength = 0;
	RIO_BUFFERID lastSendBufferId = RIO_INVALID_BUFFERID;
	PRIO_BUF lastSendRemoteAddress = nullptr;

	bool initializeSessionRIOReturn = true;
	int initializeSessionRIOCallCount = 0;

	ULONG dequeueCompletionsReturn = 0;
	int dequeueCompletionsCallCount = 0;
	std::function<ULONG(ThreadIdType, RIORESULT*, ULONG)> onDequeueCompletions;

	RIO_BUFFERID registerRIOBufferReturn = reinterpret_cast<RIO_BUFFERID>(1);
	int registerRIOBufferCallCount = 0;

	int deregisterBufferCallCount = 0;

	bool rioReceiveExReturn = true;
	int rioReceiveExCallCount = 0;
	std::function<bool()> onRIOReceiveEx;
};
