#include "PreCompile.h"
#include "RIOManager.h"
#include "RUDPSession.h"
#include "Logger.h"
#include "LogExtension.h"
#include "RUDPSessionFunctionDelegate.h"
#include "../Common/etc/UtilFunc.h"

RIOManager::~RIOManager()
{
	Shutdown();
}

bool RIOManager::Initialize(const size_t numOfSockets, const size_t inNumOfWorkerThreads)
{
	if (isInitialized == true)
	{
		return true;
	}

	numOfWorkerThreads = inNumOfWorkerThreads;
	if (const auto result = LoadRIOFunctionTable(); not result)
	{
		LOG_ERROR("Failed to load RIO function table");
		return false;
	}

	rioCompletionQueues.reserve(numOfWorkerThreads);
	const size_t queueSize = numOfSockets / numOfWorkerThreads * MAX_SEND_BUFFER_SIZE;
	for (size_t i = 0; i < numOfWorkerThreads; ++i)
	{
		auto rioCQ = CreateCompletionQueue(queueSize);
		if (rioCQ == RIO_INVALID_CQ)
		{
			LOG_ERROR("Failed to create RIO completion queue");
			return false;
		}

		rioCompletionQueues.emplace_back(rioCQ);
	}

	isInitialized = true;
	return true;
}

void RIOManager::Shutdown()
{
	if (not isInitialized)
	{
		return;
	}

	rioCompletionQueues.clear();
	registeredBuffers.clear();

	isInitialized = false;
}

RIO_BUFFERID RIOManager::RegisterRIOBuffer(char* targetBuffer, const unsigned int targetBufferSize)
{
	if (not isInitialized || targetBuffer == nullptr)
	{
		return RIO_INVALID_BUFFERID;
	}

	const RIO_BUFFERID bufferId = rioFunctionTable.RIORegisterBuffer(targetBuffer, targetBufferSize);
	if (bufferId == RIO_INVALID_BUFFERID)
	{
		LOG_ERROR("RIORegisterBuffer failed");
		return RIO_INVALID_BUFFERID;
	}

	{
		std::unique_lock lock(registeredBufferMutex);
		registeredBuffers.emplace_back(bufferId);
	}

	return bufferId;
}

void RIOManager::DeregisterBuffer(RIO_BUFFERID bufferId)
{
	if (not isInitialized)
	{
		return;
	}

	std::unique_lock lock(registeredBufferMutex);
	if (const auto it = std::ranges::find(registeredBuffers, bufferId); it != registeredBuffers.end())
	{
		registeredBuffers.erase(it);
	}

	rioFunctionTable.RIODeregisterBuffer(bufferId);
}

bool RIOManager::InitializeSessionRIO(RUDPSession& session, const ThreadIdType threadId) const
{
	if (not isInitialized)
	{
		return false;
	}

	if (threadId >= rioCompletionQueues.size())
	{
		LOG_ERROR(std::format("Invalid threadId for RIO completion queue {}", threadId));
		return false;
	}

	const RIO_CQ recvCQ = rioCompletionQueues[threadId];
	const RIO_CQ sendCQ = rioCompletionQueues[threadId];
	if (not RUDPSessionFunctionDelegate::InitializeSessionRIO(session, GetRIOFunctionTable(), recvCQ, sendCQ))
	{
		LOG_ERROR("Failed to initialize RIO for session");
		return false;
	}

	return true;
}

RIO_CQ RIOManager::GetCompletionQueue(const ThreadIdType threadId) const
{
	if (not isInitialized || threadId >= rioCompletionQueues.size())
	{
		return RIO_INVALID_CQ;
	}

	return rioCompletionQueues[threadId];
}

const RIO_EXTENSION_FUNCTION_TABLE& RIOManager::GetRIOFunctionTable() const
{
	return rioFunctionTable;
}
ULONG RIOManager::DequeueCompletions(const ThreadIdType threadId, RIORESULT* results, const ULONG maxResults) const
{
	if (not isInitialized || threadId >= rioCompletionQueues.size())
	{
		return 0;
	}

	return rioFunctionTable.RIODequeueCompletion(rioCompletionQueues[threadId], results, maxResults);
}

bool RIOManager::RIOReceiveEx(const RIO_RQ& rioRQ,
	PRIO_BUF rioBuffer,
	DWORD bufferCount,
	PRIO_BUF localAddr,
	PRIO_BUF remoteAddr,
	PRIO_BUF controlContext,
	PRIO_BUF flagsContext,
	ULONG flags,
	PVOID requestContext) const
{
	if (not isInitialized)
	{
		return false;
	}

	return rioFunctionTable.RIOReceiveEx(rioRQ
		, rioBuffer
		, bufferCount
		, localAddr
		, remoteAddr
		, controlContext
		, flagsContext
		, flags
		, requestContext);
}

bool RIOManager::RIOSendEx(const RIO_RQ& rioRQ,
	PRIO_BUF rioBuffer,
	DWORD bufferCount,
	PRIO_BUF localAddr,
	PRIO_BUF remoteAddr,
	PRIO_BUF controlContext,
	PRIO_BUF flagsContext,
	ULONG flags,
	PVOID requestContext) const
{
	if (not isInitialized)
	{
		return false;
	}

	return rioFunctionTable.RIOSendEx(rioRQ
		, rioBuffer
		, bufferCount
		, localAddr
		, remoteAddr
		, controlContext
		, flagsContext
		, flags
		, requestContext);
}

bool RIOManager::LoadRIOFunctionTable()
{
	GUID guid = WSAID_MULTIPLE_RIO;
	DWORD bytes = 0;

	// For the purpose of obtaining the function table, any of the created sessions selected
	const SOCKET tempSocket = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_OVERLAPPED);
	if (tempSocket == INVALID_SOCKET)
	{
		LOG_ERROR(std::format("WSASocketW failed with error code {}", WSAGetLastError()));
		return false;
	}

	auto raii = Util::ScopeExit([tempSocket]()
		{
			closesocket(tempSocket);
		});

	if (WSAIoctl(tempSocket
		, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER
		, &guid
		, sizeof(GUID)
		, &rioFunctionTable
		, sizeof(rioFunctionTable)
		, &bytes
		, nullptr
		, nullptr) != 0)
	{
		LOG_ERROR(std::format("WSAIoctl_SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER failed with error code {}", WSAGetLastError()));
		return false;
	}

	return true;
}

RIO_CQ RIOManager::CreateCompletionQueue(const size_t queueSize) const
{
	if (isInitialized == true)
	{
		return RIO_INVALID_CQ;
	}

	const RIO_CQ rioCq = rioFunctionTable.RIOCreateCompletionQueue(static_cast<ULONG>(queueSize), nullptr);
	if (rioCq == RIO_INVALID_CQ)
	{
		LOG_ERROR(std::format("RIOCreateCompletionQueue failed with error code {}", WSAGetLastError()));
		return RIO_INVALID_CQ;
	}

	return rioCq;
}

void RIOManager::CleanupCompletionQueues()
{
	if (not isInitialized)
	{
		return;
	}

	for (const auto& rioCQ : rioCompletionQueues)
	{
		if (rioCQ != RIO_INVALID_CQ)
		{
			rioFunctionTable.RIOCloseCompletionQueue(rioCQ);
		}
	}
	rioCompletionQueues.clear();
}

void RIOManager::CleanupRegisteredBuffers()
{
	if (not isInitialized)
	{
		return;
	}

	{
		std::unique_lock lock(registeredBufferMutex);
		for (const auto& bufferId : registeredBuffers)
		{
			rioFunctionTable.RIODeregisterBuffer(bufferId);
		}
		registeredBuffers.clear();
	}
}