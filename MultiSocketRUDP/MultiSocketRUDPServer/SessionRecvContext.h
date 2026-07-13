#pragma once
#include <MSWSock.h>
#include "../Common/etc/CoreType.h"
#include "RecvBuffer.h"

class RUDPSession;

class SessionRecvContext
{
    friend class SessionRIOContext;

public:
    SessionRecvContext() = default;
    ~SessionRecvContext() = default;

    SessionRecvContext(const SessionRecvContext&) = delete;
    SessionRecvContext& operator=(const SessionRecvContext&) = delete;
    SessionRecvContext(SessionRecvContext&&) = delete;
    SessionRecvContext& operator=(SessionRecvContext&&) = delete;

public:
    // ----------------------------------------
    // @brief RIO 수신용 IOContext를 생성하고 데이터 및 주소 버퍼를 RIO에 등록합니다.
    // @param rioFunctionTable RIO 확장 함수 테이블
    // @param sessionId 세션 식별자
    // @param ownerSession 해당 컨텍스트를 소유하는 RUDPSession 포인터
    // @return 초기화 성공 여부 (true: 성공, false: 실패)
    // ----------------------------------------
    [[nodiscard]]
    bool Initialize(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable, SessionIdType sessionId, RUDPSession* ownerSession);
    // ----------------------------------------
    // @brief 등록된 RIO 버퍼들을 Deregister 합니다.
    // @param rioFunctionTable RIO 확장 함수 테이블
    // ----------------------------------------
    void Cleanup(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable) const;

    // ----------------------------------------
    // @brief NetBuffer를 수신 버퍼 리스트에 추가합니다.
    // @param buffer 추가할 NetBuffer 포인터
    // ----------------------------------------
    void EnqueueToRecvBufferList(NetBuffer* buffer);
    // ----------------------------------------
    // @brief 내부 RecvBuffer 객체를 반환합니다.
    // @return RecvBuffer 참조
    // ----------------------------------------
    [[nodiscard]]
	RecvBuffer& GetRecvBuffer();
    // ----------------------------------------
    // @brief 현재 수신 IOContext를 가리키는 shared_ptr을 반환합니다.
    // @return 수신 IOContext를 가리키는 shared_ptr
    // ----------------------------------------
    [[nodiscard]]
	std::shared_ptr<IOContext> GetRecvBufferContext() const;
    // ----------------------------------------
    // @brief 수신 IOContext의 소유권을 해제합니다.
    // ----------------------------------------
    void RecvContextReset();

private:
    // ----------------------------------------
    // @brief RIO 버퍼를 Deregister하고 유효하지 않은 상태로 설정합니다.
    // @param rioFunctionTable RIO 확장 함수 테이블
    // @param bufferId 등록 해제할 RIO 버퍼 ID
    // ----------------------------------------
    static void UnregisterRIOBuffer(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable, RIO_BUFFERID& bufferId);

private:
    RecvBuffer recvBuffer;
};