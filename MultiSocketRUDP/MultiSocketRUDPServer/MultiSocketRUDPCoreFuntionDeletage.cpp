#include "PreCompile.h"
#include "MultiSocketRUDPCoreFuntionDeletage.h"
#include "MultiSocketRUDPCore.h"

void MultiSocketRUDPCoreFunctionDelegate::EnqueueContextResult(const IOContext* contextResult, const BYTE threadId)
{
    const auto& inst = Instance();
    assert(inst.core != nullptr);

    inst.core->EnqueueContextResult(contextResult, threadId);
}

RUDPSession* MultiSocketRUDPCoreFunctionDelegate::AcquireSession()
{
	const auto& inst = Instance();

	assert(inst.core != nullptr);
	return inst.core->AcquireSession();
}

CONNECT_RESULT_CODE MultiSocketRUDPCoreFunctionDelegate::InitReserveSession(RUDPSession& session)
{
	const auto& inst = Instance();

	assert(inst.core != nullptr);
	return inst.core->InitReserveSession(session);
}

void MultiSocketRUDPCoreFunctionDelegate::DisconnectSession(const SessionIdType sessionId)
{
	const auto& inst = Instance();

	assert(inst.core != nullptr);
	inst.core->DisconnectSession(sessionId);
}

void MultiSocketRUDPCoreFunctionDelegate::PushToDisconnectTargetSession(RUDPSession& session)
{
	const auto& inst = Instance();

	assert(inst.core != nullptr);
	inst.core->PushToDisconnectTargetSession(session);
}