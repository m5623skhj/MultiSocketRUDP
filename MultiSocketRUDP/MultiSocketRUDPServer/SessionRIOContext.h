#pragma once
#include <MSWSock.h>
#include "../Common/etc/CoreType.h"
#include "SessionRecvContext.h"
#include "SessionSendContext.h"

class RUDPSession;

class SessionRIOContext
{
public:
    SessionRIOContext() = default;
    ~SessionRIOContext() = default;

    SessionRIOContext(const SessionRIOContext&) = delete;
    SessionRIOContext& operator=(const SessionRIOContext&) = delete;
    SessionRIOContext(SessionRIOContext&&) = delete;
    SessionRIOContext& operator=(SessionRIOContext&&) = delete;

public:
    // ----------------------------------------
    // @brief RIO 수신/송신 컨텍스트를 초기화하고 Request Queue를 생성합니다. 
    // @param rioFunctionTable RIO 확장 함수 테이블
    // @param rioRecvCQ 수신용 Completion Queue
    // @param rioSendCQ 송신용 Completion Queue
    // @param sock RIO에 사용할 소켓
    // @param sessionId 세션 식별자
    // @param ownerSession 해당 컨텍스트를 소유하는 RUDPSession 포인터
    // @param pendingQueueCapacity 펜딩 패킷 큐의 최대 용량입니다.
    // @return 초기화 성공 여부 (true: 성공, false: 실패)
    // ----------------------------------------
    [[nodiscard]]
    bool Initialize(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable,
        const RIO_CQ& rioRecvCQ,
        const RIO_CQ& rioSendCQ,
        SOCKET sock,
        SessionIdType sessionId,
        RUDPSession* ownerSession,
        unsigned short pendingQueueCapacity);

    // ----------------------------------------
    // @brief 내부 수신/송신 컨텍스트의 RIO 자원을 정리합니다.
    // @param rioFunctionTable RIO 확장 함수 테이블
    // ----------------------------------------
    void Cleanup(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable);

    // ----------------------------------------
    // @brief NetBuffer를 수신 버퍼 리스트에 추가합니다.
    // @param buffer 추가할 NetBuffer 포인터
    // ----------------------------------------
    void EnqueueToRecvBufferList(NetBuffer* buffer);
    // ----------------------------------------
    // @brief 내부 수신 버퍼 객체를 반환합니다.
    // @return RecvBuffer 참조
    // ----------------------------------------
    [[nodiscard]]
	RecvBuffer& GetRecvBuffer();
    // ----------------------------------------
    // @brief 현재 수신 IOContext를 가리키는 shared_ptr을 반환합니다.
    // @return 수신 IOContext shared_ptr
    // ----------------------------------------
    [[nodiscard]]
	std::shared_ptr<IOContext> GetRecvBufferContext() const;
    // ----------------------------------------
    // @brief 수신 IOContext의 소유권을 해제합니다.
    // ----------------------------------------
    void RecvContextReset();

    // ----------------------------------------
    // @brief 송신 컨텍스트에 대한 참조를 반환합니다.
    // @return SessionSendContext 참조
    // ----------------------------------------
    [[nodiscard]]
	SessionSendContext& GetSendContext();
    // ----------------------------------------
    // @brief 읽기 전용 송신 컨텍스트에 대한 참조를 반환합니다.
    // @return const SessionSendContext 참조
    // ----------------------------------------
    [[nodiscard]]
	const SessionSendContext& GetSendContext() const;

    // ----------------------------------------
    // @brief 생성된 RIO Request Queue 핸들을 반환합니다.
    // @return RIO_RQ 값
    // ----------------------------------------
    [[nodiscard]]
	RIO_RQ GetRIORQ() const;

private:
    SessionIdType cachedSessionId = INVALID_SESSION_ID;
    RIO_RQ rioRQ = RIO_INVALID_RQ;

    SessionRecvContext recvContext;
    SessionSendContext sendContext;
};