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
	[[nodiscard]]
	bool RIOReceiveEx(const RIO_RQ& rioRQ,
		PRIO_BUF rioBuffer,
		DWORD bufferCount,
		PRIO_BUF localAddr,
		PRIO_BUF remoteAddr,
		PRIO_BUF controlContext,
		PRIO_BUF flagsContext,
		ULONG flags,
		PVOID requestContext) const;

	// ----------------------------------------
	// @brief 버퍼에 있는 데이터를 원격 대상으로 전송합니다.
	// @param rioRQ RIO 요청 큐 핸들입니다.
	// @param rioBuffer 송신할 데이터 버퍼입니다.
	// @param bufferCount rioBuffer에 있는 RIO_BUF 구조체의 수입니다.
	// @param localAddr 송신에 사용될 로컬 주소 정보입니다.
	// @param remoteAddr 원격 대상의 주소 정보입니다.
	// @param controlContext 제어 정보를 포함하는 버퍼입니다.
	// @param flagsContext 플래그 정보를 포함하는 버퍼입니다.
	// @param flags 송신 작업의 동작을 제어하는 플래그입니다.
	// @param requestContext 작업 완료 시 반환될 컨텍스트 값입니다.
	// @return 함수 성공 시 true, 실패 시 false를 반환합니다.
	// ----------------------------------------
	[[nodiscard]]
	bool RIOSendEx(const RIO_RQ& rioRQ,
		PRIO_BUF rioBuffer,
		DWORD bufferCount,
		PRIO_BUF localAddr,
		PRIO_BUF remoteAddr,
		PRIO_BUF controlContext,
		PRIO_BUF flagsContext,
		ULONG flags,
		PVOID requestContext) const;

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

	bool isInitialized{};
	size_t numOfWorkerThreads{};
};
