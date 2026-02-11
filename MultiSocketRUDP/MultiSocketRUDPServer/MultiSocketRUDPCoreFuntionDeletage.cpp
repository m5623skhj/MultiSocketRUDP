#include "PreCompile.h"
#include "MultiSocketRUDPCoreFuntionDeletage.h"
#include "MultiSocketRUDPCore.h"

void MultiSocketRUDPCoreFunctionDelegate::EnqueueContextResult(IOContext* contextResult, const BYTE threadId)
{
    const auto& inst = Instance();
    assert(inst.core != nullptr);

    inst.core->EnqueueContextResult(contextResult, threadId);
}
