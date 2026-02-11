#pragma once
#include <cassert>

struct IOContext;

class MultiSocketRUDPCore;
class RUDPIOHandler;

class MultiSocketRUDPCoreFunctionDelegate
{
    friend MultiSocketRUDPCore;
    friend RUDPIOHandler;

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

private:
    MultiSocketRUDPCore* core = nullptr;
};