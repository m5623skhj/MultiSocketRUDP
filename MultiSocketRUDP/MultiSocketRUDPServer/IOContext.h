#pragma once
#include <MSWSock.h>

class RUDPSession;

struct IOContext : RIO_BUF
{
	IOContext() = default;
	~IOContext() = default;

	void InitContext(const SessionIdType inOwnerSessionId, const RIO_OPERATION_TYPE inIOType)
	{
		ownerSessionId = inOwnerSessionId;
		ioType = inIOType;
	}

	SessionIdType ownerSessionId = INVALID_SESSION_ID;
	RIO_OPERATION_TYPE ioType = RIO_OPERATION_TYPE::OP_ERROR;
	RUDPSession* session = nullptr;
	RIO_BUF clientAddrRIOBuffer{ RIO_INVALID_BUFFERID, };
	RIO_BUF localAddrRIOBuffer{ RIO_INVALID_BUFFERID, };
	char clientAddrBuffer[sizeof(SOCKADDR_INET)];
	char localAddrBuffer[sizeof(SOCKADDR_INET)];
};
