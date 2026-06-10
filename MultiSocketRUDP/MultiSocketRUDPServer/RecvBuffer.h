#pragma once
#include "IOContext.h"
#include "Queue.h"
#include "NetServerSerializeBuffer.h"
#include "../Common/etc/CoreType.h"

struct RecvBufferSlot
{
    std::shared_ptr<IOContext> recvContext{};
    char buffer[RECV_BUFFER_SIZE];
};

struct RecvBuffer
{
    std::array<RecvBufferSlot, RECV_OUTSTANDING_COUNT> slots{};
    CListBaseQueue<IOContext*> freeRecvContexts;
    CListBaseQueue<NetBuffer*> recvBufferList;

    IOContext* AcquireFreeRecvContext()
    {
        IOContext* context = nullptr;
        if (not freeRecvContexts.Dequeue(&context))
        {
            return nullptr;
        }

        return context;
    }

    void ReleaseRecvContext(IOContext* context)
    {
        freeRecvContexts.Enqueue(context);
    }

    void ClearFreeRecvContexts()
    {
        IOContext* drained = nullptr;
        while (freeRecvContexts.Dequeue(&drained))
        {
        }
    }
};