#pragma once
#include <cassert>
#include "../Common/etc/CoreType.h"

enum class CONNECT_RESULT_CODE : unsigned char;
struct IOContext;

class RUDPSession;
class MultiSocketRUDPCore;
class RUDPIOHandler;
class RUDPSessionBroker;

class MultiSocketRUDPCoreFunctionDelegate
{
    friend RUDPSession;
    friend MultiSocketRUDPCore;
    friend RUDPIOHandler;
    friend RUDPSessionBroker;

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

    void Init(MultiSocketRUDPCore& inCore)
    {
        assert(core == nullptr);
        core = &inCore;
    }

private:
    static void EnqueueContextResult(const IOContext* contextResult, BYTE threadId);
	static RUDPSession* AcquireSession();
	static CONNECT_RESULT_CODE InitReserveSession(OUT RUDPSession& session);
    static void DisconnectSession(SessionIdType sessionId);
    static void PushToDisconnectTargetSession(RUDPSession& session);

private:
    MultiSocketRUDPCore* core = nullptr;
};