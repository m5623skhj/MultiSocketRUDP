#pragma once
#include <MSWSock.h>
#include "../Common/etc/CoreType.h"

struct SendPacketInfo;

class ICore
{
public:
	virtual ~ICore() = default;

	[[nodiscard]]
	virtual bool SendPacket(SendPacketInfo* sendPacketInfo, bool needAddRefCount = true) const = 0;
	virtual void PushToDisconnectTargetSession(RUDPSession& session) = 0;
	virtual void EraseSendPacketInfo(OUT SendPacketInfo* eraseTarget, ThreadIdType threadId) = 0;

	virtual void DisconnectSession(SessionIdType disconnectTargetSessionId) const = 0;

	[[nodiscard]]
	virtual RIO_EXTENSION_FUNCTION_TABLE GetRIOFunctionTable() const = 0;
};
