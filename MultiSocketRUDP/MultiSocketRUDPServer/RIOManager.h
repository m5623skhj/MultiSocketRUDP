#pragma once

#include <MSWSock.h>
#include <vector>
#include <mutex>
#include "../Common/etc/CoreType.h"

class RUDPSession;

class RIOManager
{
public:
	RIOManager() = default;
	~RIOManager();

public:
	[[nodiscard]]
	bool Initialize(size_t numOfSockets, size_t inNumOfWorkerThreads);
	void Shutdown();

	[[nodiscard]]
	RIO_BUFFERID RegisterRIOBuffer(char* targetBuffer, unsigned int targetBufferSize);
	void DeregisterBuffer(RIO_BUFFERID bufferId);

	[[nodiscard]]
	bool InitializeSessionRIO(RUDPSession& session, ThreadIdType threadId) const;

	[[nodiscard]]
	RIO_CQ GetCompletionQueue(ThreadIdType threadId) const;
	[[nodiscard]]
	const RIO_EXTENSION_FUNCTION_TABLE& GetRIOFunctionTable() const;
	[[nodiscard]]
	ULONG DequeueCompletions(ThreadIdType threadId, RIORESULT* results, ULONG maxResults) const;

private:
	[[nodiscard]]
	bool LoadRIOFunctionTable();
	[[nodiscard]]
	RIO_CQ CreateCompletionQueue(size_t queueSize) const;

private:
	void CleanupCompletionQueues();
	void CleanupRegisteredBuffers();

private:
	RIO_EXTENSION_FUNCTION_TABLE rioFunctionTable{};
	std::vector<RIO_CQ> rioCompletionQueues;

	std::vector<RIO_BUFFERID> registeredBuffers;
	std::mutex registeredBufferMutex;

	bool initialized{};
	size_t numOfWorkerThreads{};
};
