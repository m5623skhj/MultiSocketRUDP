#pragma once
#include <MSWSock.h>
#include "../Common/etc/CoreType.h"

struct SendPacketInfo;

class RUDPSession;

// ----------------------------------------
// @brief RUDPSession이 서버 코어의 송신 및 연결 해제 기능을 호출하기 위한 내부 인터페이스입니다.
// 세션이 구체적인 MultiSocketRUDPCore 구현에 직접 의존하지 않도록 경계를 제공합니다.
// ----------------------------------------
class ICore
{
public:
	virtual ~ICore() = default;

	[[nodiscard]]
	// ----------------------------------------
	// @brief 송신 정보를 세션의 RIO 송신 큐에 별도 참조와 함께 등록하고 송신 처리를 시작합니다.
	// @param sendPacketInfo 호출자가 보유 참조를 유지한 채 전달하는 송신 정보입니다.
	// @return 입력과 소유자가 유효하고 송신 처리를 시작했으면 true를 반환합니다.
	// ----------------------------------------
	virtual bool SendPacket(SendPacketInfo* sendPacketInfo) const = 0;
	// ----------------------------------------
	// @brief 세션을 비동기 연결 해제 대상 큐에 추가합니다.
	// ----------------------------------------
	virtual void PushToDisconnectTargetSession(RUDPSession& session) = 0;
	// ----------------------------------------
	// @brief 지정 스레드의 재전송 스케줄러 잠금 아래 패킷 정보를 재전송 제외 상태로 표시합니다.
	// ----------------------------------------
	virtual void MarkSendPacketInfoErased(OUT SendPacketInfo* eraseTarget, ThreadIdType threadId) = 0;

	// ----------------------------------------
	// @brief 세션 ID에 해당하는 활성 세션의 연결 해제를 요청합니다.
	// ----------------------------------------
	virtual void DisconnectSession(SessionIdType disconnectTargetSessionId) const = 0;

	[[nodiscard]]
	// ----------------------------------------
	// @brief 서버가 로드한 Windows RIO 확장 함수 테이블의 복사본을 반환합니다.
	// ----------------------------------------
	virtual RIO_EXTENSION_FUNCTION_TABLE GetRIOFunctionTable() const = 0;
};
