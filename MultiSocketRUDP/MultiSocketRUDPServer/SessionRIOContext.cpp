#include "PreCompile.h"
#include "SessionRIOContext.h"
#include "RUDPSession.h"
#include "LogExtension.h"
#include "Logger.h"
#include <MSWSock.h>

bool SessionRIOContext::Initialize(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable,
    const RIO_CQ& rioRecvCQ,
    const RIO_CQ& rioSendCQ,
    const SOCKET sock,
    const SessionIdType sessionId,
    RUDPSession* ownerSession,
    unsigned short pendingQueueCapacity)
{
    cachedSessionId = sessionId;
    if (not recvContext.Initialize(rioFunctionTable, sessionId, ownerSession))
    {
        LOG_ERROR("SessionRIOContext: recvContext.Initialize failed");
        return false;
    }

    if (not sendContext.Initialize(rioFunctionTable, pendingQueueCapacity))
    {
        LOG_ERROR("SessionRIOContext: sendContext.Initialize failed");
        return false;
    }

    rioRQ = rioFunctionTable.RIOCreateRequestQueue(sock, 1, 1, 1, 1, rioRecvCQ, rioSendCQ, &cachedSessionId);
    if (rioRQ == RIO_INVALID_RQ)
    {
        LOG_ERROR(std::format("RIOCreateRequestQueue failed with error {}", WSAGetLastError()));
        return false;
    }

    return true;
}

void SessionRIOContext::Cleanup(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable)
{
    recvContext.Cleanup(rioFunctionTable);
    sendContext.Cleanup(rioFunctionTable);
}

void SessionRIOContext::EnqueueToRecvBufferList(NetBuffer* buffer)
{
    recvContext.EnqueueToRecvBufferList(buffer);
}

RecvBuffer& SessionRIOContext::GetRecvBuffer()
{
    return recvContext.GetRecvBuffer();
}

std::shared_ptr<IOContext> SessionRIOContext::GetRecvBufferContext() const
{
    return recvContext.GetRecvBufferContext();
}

void SessionRIOContext::RecvContextReset()
{
    recvContext.RecvContextReset();
}

SessionSendContext& SessionRIOContext::GetSendContext()
{
	return sendContext;
}

const SessionSendContext& SessionRIOContext::GetSendContext() const
{
	return sendContext;
}

RIO_RQ SessionRIOContext::GetRIORQ() const
{
	return rioRQ;
}