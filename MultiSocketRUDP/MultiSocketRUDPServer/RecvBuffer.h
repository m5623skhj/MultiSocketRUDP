#pragma once
#include "IOContext.h"
#include "Queue.h"
#include "NetServerSerializeBuffer.h"
#include "../Common/etc/CoreType.h"

struct RecvBuffer
{
    std::shared_ptr<IOContext> recvContext{};
    char buffer[RECV_BUFFER_SIZE];
    CListBaseQueue<NetBuffer*> recvBufferList;
};