#pragma once
#include <MSWSock.h>
#include "../Common/etc/CoreType.h"

class RUDPSession;

class IRIOManager
{
public:
	virtual ~IRIOManager() = default;

	virtual bool RIOReceiveEx(
		const RIO_RQ& rioRQ,
		PRIO_BUF rioBuffer,
		DWORD bufferCount,
		PRIO_BUF localAddr,
		PRIO_BUF remoteAddr,
		PRIO_BUF controlContext,
		PRIO_BUF flagsContext,
		ULONG flags,
		PVOID requestContext) const = 0;

	virtual bool RIOSendEx(
		const RIO_RQ& rioRQ,
		PRIO_BUF rioBuffer,
		DWORD bufferCount,
		PRIO_BUF localAddr,
		PRIO_BUF remoteAddr,
		PRIO_BUF controlContext,
		PRIO_BUF flagsContext,
		ULONG flags,
		PVOID requestContext) const = 0;

	virtual RIO_BUFFERID RegisterRIOBuffer(char* targetBuffer, unsigned int targetBufferSize) = 0;
	virtual void DeregisterBuffer(RIO_BUFFERID bufferId) = 0;

	virtual ULONG DequeueCompletions(ThreadIdType threadId, RIORESULT* results, ULONG maxResults) const = 0;

	virtual bool InitializeSessionRIO(RUDPSession& session, ThreadIdType threadId) const = 0;

	virtual const RIO_EXTENSION_FUNCTION_TABLE& GetRIOFunctionTable() const = 0;
};
