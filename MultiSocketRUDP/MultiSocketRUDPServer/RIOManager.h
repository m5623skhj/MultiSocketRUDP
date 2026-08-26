#pragma once
#include "IRIOManager.h"
#include <MSWSock.h>
#include <vector>
#include <mutex>
#include "../Common/etc/CoreType.h"

class RUDPSession;
class ISessionDelegate;

// ----------------------------------------
// @brief RIO 함수 테이블, 워커별 완료 큐 및 등록 버퍼의 수명을 관리하는 구현체입니다.
// 등록 버퍼 목록은 registeredBufferMutex로 보호되며 Shutdown은 모든 세션 I/O가 중단된 뒤 호출해야 합니다.
// ----------------------------------------
class RIOManager : public IRIOManager
{
public:
	explicit RIOManager(ISessionDelegate& inSessionDelegate);
	~RIOManager();

public:
	// ----------------------------------------
	// @brief RIO 함수 테이블과 워커별 완료 큐를 생성합니다.
	// ----------------------------------------
	[[nodiscard]]
	bool Initialize(size_t numOfSockets, size_t inNumOfWorkerThreads);
	// ----------------------------------------
	// @brief 완료 큐와 추적 중인 등록 버퍼를 모두 정리합니다.
	// ----------------------------------------
	void Shutdown();

	[[nodiscard]]
	RIO_BUFFERID RegisterRIOBuffer(char* targetBuffer, unsigned int targetBufferSize);
	void DeregisterBuffer(RIO_BUFFERID bufferId);

	[[nodiscard]]
	bool InitializeSessionRIO(RUDPSession& session, ThreadIdType threadId) const;

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
	ISessionDelegate& sessionDelegate;

	RIO_EXTENSION_FUNCTION_TABLE rioFunctionTable{};
	std::vector<RIO_CQ> rioCompletionQueues;

	std::vector<RIO_BUFFERID> registeredBuffers;
	std::mutex registeredBufferMutex;

	bool isInitialized{};
	size_t numOfWorkerThreads{};
};
