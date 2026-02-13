#pragma once
#include <cassert>

enum class CONNECT_RESULT_CODE : unsigned char;
class RUDPSession;
struct IOContext;

class MultiSocketRUDPCore;
class RUDPIOHandler;
class RUDPSessionBroker;

class MultiSocketRUDPCoreFunctionDelegate
{
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
    static void EnqueueContextResult(IOContext* contextResult, BYTE threadId);
	static RUDPSession* AcquireSession();
	static CONNECT_RESULT_CODE InitReserveSession(OUT RUDPSession& session);

private:
    MultiSocketRUDPCore* core = nullptr;
};