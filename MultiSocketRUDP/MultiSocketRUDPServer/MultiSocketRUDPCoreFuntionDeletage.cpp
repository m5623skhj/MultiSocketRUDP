#include "PreCompile.h"
#include "MultiSocketRUDPCoreFuntionDeletage.h"
#include "MultiSocketRUDPCore.h"

void MultiSocketRUDPCoreFunctionDelegate::EnqueueContextResult(IOContext* contextResult, const BYTE threadId)
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
