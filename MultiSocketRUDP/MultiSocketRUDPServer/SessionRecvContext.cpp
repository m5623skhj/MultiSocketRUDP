#include "PreCompile.h"
#include "SessionRecvContext.h"
#include "RUDPSession.h"
#include "Logger.h"

bool SessionRecvContext::Initialize(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable, const SessionIdType sessionId, RUDPSession* ownerSession)
{
    recvBuffer.recvContext = std::make_shared<IOContext>();
    if (recvBuffer.recvContext == nullptr)
    {
        return false;
    }

    const auto& context = recvBuffer.recvContext;
    context->InitContext(sessionId, RIO_OPERATION_TYPE::OP_RECV);
    context->Length = RECV_BUFFER_SIZE;
    context->Offset = 0;
    context->session = ownerSession;

    context->clientAddrRIOBuffer.Length = sizeof(SOCKADDR_INET);
    context->clientAddrRIOBuffer.Offset = 0;
    context->localAddrRIOBuffer.Length = sizeof(SOCKADDR_INET);
    context->localAddrRIOBuffer.Offset = 0;

    context->BufferId = rioFunctionTable.RIORegisterBuffer(recvBuffer.buffer, RECV_BUFFER_SIZE);
    context->clientAddrRIOBuffer.BufferId = rioFunctionTable.RIORegisterBuffer(context->clientAddrBuffer, sizeof(SOCKADDR_INET));
    context->localAddrRIOBuffer.BufferId = rioFunctionTable.RIORegisterBuffer(context->localAddrBuffer, sizeof(SOCKADDR_INET));

    if (context->BufferId == RIO_INVALID_BUFFERID ||
        context->clientAddrRIOBuffer.BufferId == RIO_INVALID_BUFFERID ||
        context->localAddrRIOBuffer.BufferId == RIO_INVALID_BUFFERID)
    {
        return false;
    }

    return true;
}

void SessionRecvContext::Cleanup(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable) const
{
    if (recvBuffer.recvContext == nullptr)
    {
        return;
    }

    const auto& ctx = recvBuffer.recvContext;
    UnregisterRIOBuffer(rioFunctionTable, ctx->BufferId);
    UnregisterRIOBuffer(rioFunctionTable, ctx->clientAddrRIOBuffer.BufferId);
    UnregisterRIOBuffer(rioFunctionTable, ctx->localAddrRIOBuffer.BufferId);
}

void SessionRecvContext::UnregisterRIOBuffer(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable, RIO_BUFFERID& bufferId)
{
    if (bufferId != RIO_INVALID_BUFFERID)
    {
        rioFunctionTable.RIODeregisterBuffer(bufferId);
        bufferId = RIO_INVALID_BUFFERID;
    }
}

void SessionRecvContext::RecvContextReset()
{
    if (recvBuffer.recvContext != nullptr)
    {
        recvBuffer.recvContext.reset();
    }
}

void SessionRecvContext::EnqueueToRecvBufferList(NetBuffer* buffer)
{
    recvBuffer.recvBufferList.Enqueue(buffer);
}

RecvBuffer& SessionRecvContext::GetRecvBuffer()
{
	return recvBuffer;
}

std::shared_ptr<IOContext> SessionRecvContext::GetRecvBufferContext() const
{
    return recvBuffer.recvContext;
}