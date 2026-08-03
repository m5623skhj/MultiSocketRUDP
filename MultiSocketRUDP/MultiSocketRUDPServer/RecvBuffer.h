#pragma once
#include "IOContext.h"
#include "Queue.h"
#include "NetServerSerializeBuffer.h"
#include "../Common/etc/CoreType.h"
#include <array>
#include <atomic>
#include <cassert>

struct RecvBufferSlot
{
    std::shared_ptr<IOContext> recvContext{};
    char buffer[RECV_BUFFER_SIZE];
};

struct RecvBuffer
{
    std::array<RecvBufferSlot, RECV_OUTSTANDING_COUNT> slots{};
    CListBaseQueue<IOContext*> freeRecvContexts;
    std::atomic_uint32_t outstandingRecvIo{};
    std::atomic_uint32_t pendingRecvLogic{};

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

    // ----------------------------------------
    // @brief Adds one successfully posted receive I/O to the drain barrier.
    // ----------------------------------------
    void BeginRecvIo()
    {
        outstandingRecvIo.fetch_add(1, std::memory_order_acq_rel);
    }

    // ----------------------------------------
    // @brief Removes one completed receive I/O from the drain barrier.
    // @details Each call must match exactly one BeginRecvIo() call.
    // ----------------------------------------
    void CompleteRecvIo()
    {
        const auto previous = outstandingRecvIo.fetch_sub(1, std::memory_order_acq_rel);
        assert(previous > 0);
    }

    // ----------------------------------------
    // @brief Adds one packet handed to a logic worker to the drain barrier.
    // ----------------------------------------
    void BeginRecvLogic()
    {
        pendingRecvLogic.fetch_add(1, std::memory_order_acq_rel);
    }

    // ----------------------------------------
    // @brief Removes one processed receive packet from the drain barrier.
    // @details Each call must match exactly one BeginRecvLogic() call.
    // ----------------------------------------
    void CompleteRecvLogic()
    {
        const auto previous = pendingRecvLogic.fetch_sub(1, std::memory_order_acq_rel);
        assert(previous > 0);
    }

    // ----------------------------------------
    // @brief Checks whether all posted receive I/O and pending receive logic are complete.
    // @return true when both tracking counters are zero.
    // ----------------------------------------
    [[nodiscard]]
    bool IsDrained() const
    {
        return outstandingRecvIo.load(std::memory_order_acquire) == 0 &&
            pendingRecvLogic.load(std::memory_order_acquire) == 0;
    }
};
