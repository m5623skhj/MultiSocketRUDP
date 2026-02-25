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
		const RIO_RQ&, PRIO_BUF, DWORD,
		PRIO_BUF, PRIO_BUF, PRIO_BUF, PRIO_BUF,
		ULONG, PVOID) const override
	{
		++const_cast<MockRIOManager*>(this)->rioSendExCallCount;
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
		registerRIOBufferCallCount = 0;
		deregisterBufferCallCount = 0;
		dequeueCompletionsCallCount = 0;
		initializeSessionRIOCallCount = 0;
	}

	mutable RIO_EXTENSION_FUNCTION_TABLE dummyTable{};

	bool rioSendExReturn = true;
	int rioSendExCallCount = 0;
	std::function<bool()> onRIOSendEx;

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
