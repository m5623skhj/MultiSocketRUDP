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
    [[nodiscard]]
    bool Initialize(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable,
        const RIO_CQ& rioRecvCQ,
        const RIO_CQ& rioSendCQ,
        SOCKET sock,
        SessionIdType sessionId,
        RUDPSession* ownerSession);

    void Cleanup(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable);

    void EnqueueToRecvBufferList(NetBuffer* buffer);
    [[nodiscard]]
	RecvBuffer& GetRecvBuffer();
    [[nodiscard]]
	std::shared_ptr<IOContext> GetRecvBufferContext() const;
    void RecvContextReset();

    [[nodiscard]]
	SessionSendContext& GetSendContext();
    [[nodiscard]]
	const SessionSendContext& GetSendContext() const;

    [[nodiscard]]
	RIO_RQ GetRIORQ() const;

private:
    SessionIdType cachedSessionId = INVALID_SESSION_ID;
    RIO_RQ rioRQ = RIO_INVALID_RQ;

    SessionRecvContext recvContext;
    SessionSendContext sendContext;
};