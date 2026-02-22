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
    [[nodiscard]]
    bool Initialize(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable, SessionIdType sessionId, RUDPSession* ownerSession);
    void Cleanup(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable) const;

    void EnqueueToRecvBufferList(NetBuffer* buffer);
    [[nodiscard]] RecvBuffer& GetRecvBuffer();
    [[nodiscard]] std::shared_ptr<IOContext> GetRecvBufferContext() const;
    void RecvContextReset();

private:
    static void UnregisterRIOBuffer(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable, RIO_BUFFERID& bufferId);

private:
    RecvBuffer recvBuffer;
};