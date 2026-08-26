#pragma once
#include <MSWSock.h>
#include "../Common/etc/CoreType.h"

class RUDPSession;

// ----------------------------------------
// @brief Windows Registered I/O 자원과 송수신 호출을 추상화하는 인터페이스입니다.
// 세션과 I/O 처리기가 RIO 초기화 구현과 분리되어 테스트 대역을 사용할 수 있게 합니다.
// ----------------------------------------
class IRIOManager
{
public:
	virtual ~IRIOManager() = default;

	// ----------------------------------------
	// @brief 등록된 요청 큐에 비동기 수신 작업을 게시합니다.
	// @return RIOReceiveEx 호출 성공 여부입니다.
	// ----------------------------------------
	virtual bool RIOReceiveEx(
		const RIO_RQ& rioRQ,
		PRIO_BUF rioBuffer,
		DWORD bufferCount,
		PRIO_BUF localAddr,
		PRIO_BUF remoteAddr,
		PRIO_BUF controlContext,
		PRIO_BUF flagsContext,
		ULONG flags,
		PVOID requestContext) const = 0;

	// ----------------------------------------
	// @brief 등록된 요청 큐에 비동기 송신 작업을 게시합니다.
	// @return RIOSendEx 호출 성공 여부입니다.
	// ----------------------------------------
	virtual bool RIOSendEx(
		const RIO_RQ& rioRQ,
		PRIO_BUF rioBuffer,
		DWORD bufferCount,
		PRIO_BUF localAddr,
		PRIO_BUF remoteAddr,
		PRIO_BUF controlContext,
		PRIO_BUF flagsContext,
		ULONG flags,
		PVOID requestContext) const = 0;

	// ----------------------------------------
	// @brief 메모리 영역을 RIO 버퍼로 등록하고 수명 추적용 ID를 반환합니다.
	// @return 등록 실패 시 RIO_INVALID_BUFFERID를 반환합니다.
	// ----------------------------------------
	virtual RIO_BUFFERID RegisterRIOBuffer(char* targetBuffer, unsigned int targetBufferSize) = 0;
	// ----------------------------------------
	// @brief 이전에 등록한 RIO 버퍼를 해제합니다.
	// ----------------------------------------
	virtual void DeregisterBuffer(RIO_BUFFERID bufferId) = 0;

	// ----------------------------------------
	// @brief 워커 스레드에 대응하는 완료 큐에서 최대 maxResults개의 결과를 꺼냅니다.
	// ----------------------------------------
	virtual ULONG DequeueCompletions(ThreadIdType threadId, RIORESULT* results, ULONG maxResults) const = 0;

	// ----------------------------------------
	// @brief 세션에 워커 스레드의 완료 큐와 RIO 요청 큐를 연결합니다.
	// ----------------------------------------
	virtual bool InitializeSessionRIO(RUDPSession& session, ThreadIdType threadId) const = 0;

	// ----------------------------------------
	// @brief 로드된 RIO 확장 함수 테이블을 반환합니다.
	// ----------------------------------------
	virtual const RIO_EXTENSION_FUNCTION_TABLE& GetRIOFunctionTable() const = 0;
};
