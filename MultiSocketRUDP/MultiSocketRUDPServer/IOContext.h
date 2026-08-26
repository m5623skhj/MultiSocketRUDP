#pragma once
#include <MSWSock.h>
#include "NetServerSerializeBuffer.h"

class RUDPSession;
struct RecvBuffer;

// ----------------------------------------
// @brief RIO 요청과 완료 결과에 연결되는 세션별 I/O 컨텍스트입니다.
// 세션 generation을 함께 저장하여 세션 풀 재사용 후 도착한 오래된 완료를 구분합니다.
// RIO_BUF를 상속하므로 이 객체 자체가 등록 버퍼의 위치와 길이 정보도 보유합니다.
// ----------------------------------------
struct IOContext : RIO_BUF
{
	IOContext() = default;
	~IOContext() = default;

	// ----------------------------------------
	// @brief 새 작업의 세션 ID와 작업 종류를 설정하고 이전 작업의 소유 포인터를 초기화합니다.
	// RIO 버퍼 ID, 오프셋 및 길이는 등록 상태를 보존하기 위해 변경하지 않습니다.
	// ----------------------------------------
	void InitContext(const SessionIdType inOwnerSessionId, const RIO_OPERATION_TYPE inIOType)
	{
		ownerSessionId = inOwnerSessionId;
		ownerSessionGeneration = 0;
		ioType = inIOType;
		session = nullptr;
		ownerRecvBuffer = nullptr;
	}

	SessionIdType ownerSessionId = INVALID_SESSION_ID;
	uint32_t ownerSessionGeneration = 0;
	RIO_OPERATION_TYPE ioType = RIO_OPERATION_TYPE::OP_ERROR;
	RUDPSession* session = nullptr;
	RecvBuffer* ownerRecvBuffer = nullptr;
	char* recvDataBuffer = nullptr;
	RIO_BUF clientAddrRIOBuffer{ RIO_INVALID_BUFFERID, };
	RIO_BUF localAddrRIOBuffer{ RIO_INVALID_BUFFERID, };
	char clientAddrBuffer[sizeof(SOCKADDR_INET)];
	char localAddrBuffer[sizeof(SOCKADDR_INET)];
};

// ----------------------------------------
// @brief RIO 완료 스레드에서 수신 로직 스레드로 전달되는 수신 완료 정보입니다.
// ownerRecvBuffer는 완료 처리 후 outstanding 수신 로직 수를 감소시키는 데 사용됩니다.
// ----------------------------------------
struct RecvIOCompletedContext
{
	RecvIOCompletedContext() = default;
	~RecvIOCompletedContext() = default;

	// ----------------------------------------
	// @brief 수신 완료 시점의 세션 generation과 원격 주소를 복사하여 고정합니다.
	// ----------------------------------------
	void InitContext(RUDPSession* inOwnerSession,
		RecvBuffer* inOwnerRecvBuffer,
		const uint32_t inOwnerSessionGeneration,
		NetBuffer* inBuffer,
		const char* inClientAddrBuffer)
	{
		session = inOwnerSession;
		ownerRecvBuffer = inOwnerRecvBuffer;
		ownerSessionGeneration = inOwnerSessionGeneration;
		buffer = inBuffer;
		memcpy(clientAddrBuffer, inClientAddrBuffer, sizeof(SOCKADDR_INET));
	}

	RUDPSession* session{};
	RecvBuffer* ownerRecvBuffer{};
	uint32_t ownerSessionGeneration{};
	NetBuffer* buffer{};
	char clientAddrBuffer[sizeof(SOCKADDR_INET)];
};
