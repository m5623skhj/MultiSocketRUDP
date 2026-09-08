#pragma once
#include <cassert>
#include "../Common/etc/CoreType.h"
#include "NetServerSerializeBuffer.h"

enum class CONNECT_RESULT_CODE : unsigned char;
struct IOContext;

class RUDPSession;
class MultiSocketRUDPCore;
class RUDPIOHandler;
class RUDPSessionBroker;

// ----------------------------------------
// @brief 순환 include 없이 세션·I/O·브로커가 서버 코어의 제한된 내부 기능을 호출하게 하는 단일 delegate입니다.
// Init과 Clear는 MultiSocketRUDPCore의 생명주기에서 정확히 한 번씩 호출되며 등록된 core보다 오래 사용하면 안 됩니다.
// friend로 지정된 서버 내부 구성 요소만 접근할 수 있습니다.
// ----------------------------------------
class MultiSocketRUDPCoreFunctionDelegate
{
    friend RUDPSession;
    friend MultiSocketRUDPCore;
    friend RUDPIOHandler;
    friend RUDPSessionBroker;
    friend class MultiSocketRUDPCoreTestAccess;

public:
    ~MultiSocketRUDPCoreFunctionDelegate() = default;
    MultiSocketRUDPCoreFunctionDelegate(const MultiSocketRUDPCoreFunctionDelegate&) = delete;
    MultiSocketRUDPCoreFunctionDelegate& operator=(const MultiSocketRUDPCoreFunctionDelegate&) = delete;
    MultiSocketRUDPCoreFunctionDelegate(MultiSocketRUDPCoreFunctionDelegate&&) = delete;
    MultiSocketRUDPCoreFunctionDelegate& operator=(MultiSocketRUDPCoreFunctionDelegate&&) = delete;

private:
    MultiSocketRUDPCoreFunctionDelegate() = default;

private:
    static MultiSocketRUDPCoreFunctionDelegate& Instance()
    {
        static MultiSocketRUDPCoreFunctionDelegate instance;
        return instance;
    }

	// ----------------------------------------
	// @brief 활성 코어를 등록합니다. 이미 등록된 상태의 중복 호출은 허용하지 않습니다.
	// ----------------------------------------
	void Init(MultiSocketRUDPCore& inCore)
    {
        assert(core == nullptr);
        core = &inCore;
    }

	// ----------------------------------------
	// @brief 동일한 코어의 종료 시 delegate 연결을 해제합니다.
	// ----------------------------------------
	void Clear(MultiSocketRUDPCore& inCore)
    {
        assert(core == &inCore);
        core = nullptr;
    }

private:
	// ----------------------------------------
	// RIO 완료 전달, 세션 예약 및 연결 해제를 코어로 중계하는 내부 API
	// ----------------------------------------
	[[nodiscard]]
	static bool EnqueueContextResult(const IOContext* contextResult, NetBuffer* buffer, BYTE threadId);
	static RUDPSession* AcquireSession();
	static CONNECT_RESULT_CODE InitReserveSession(OUT RUDPSession& session);
    static void DisconnectSession(SessionIdType sessionId);
    static void PushToDisconnectTargetSession(RUDPSession& session);

private:
    MultiSocketRUDPCore* core = nullptr;
};
