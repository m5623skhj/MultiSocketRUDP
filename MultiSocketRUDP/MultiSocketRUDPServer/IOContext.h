#pragma once
#include <MSWSock.h>
#include "NetServerSerializeBuffer.h"

class RUDPSession;
struct RecvBuffer;

struct IOContext : RIO_BUF
{
	IOContext() = default;
	~IOContext() = default;

	void InitContext(const SessionIdType inOwnerSessionId, const RIO_OPERATION_TYPE inIOType)
	{
		ownerSessionId = inOwnerSessionId;
		ownerSessionGeneration = 0;
		ioType = inIOType;
		session = nullptr;
		ownerRecvBuffer = nullptr;
	}

	SessionIdType ownerSessionId = INVALID_SESSION_ID;
	uint32_t ownerSessionGeneration = 0;
	RIO_OPERATION_TYPE ioType = RIO_OPERATION_TYPE::OP_ERROR;
	RUDPSession* session = nullptr;
	RecvBuffer* ownerRecvBuffer = nullptr;
	char* recvDataBuffer = nullptr;
	RIO_BUF clientAddrRIOBuffer{ RIO_INVALID_BUFFERID, };
	RIO_BUF localAddrRIOBuffer{ RIO_INVALID_BUFFERID, };
	char clientAddrBuffer[sizeof(SOCKADDR_INET)];
	char localAddrBuffer[sizeof(SOCKADDR_INET)];
};

struct RecvIOCompletedContext
{
	RecvIOCompletedContext() = default;
	~RecvIOCompletedContext() = default;

	void InitContext(RUDPSession* inOwnerSession,
		RecvBuffer* inOwnerRecvBuffer,
		const uint32_t inOwnerSessionGeneration,
		NetBuffer* inBuffer,
		const char* inClientAddrBuffer)
	{
		session = inOwnerSession;
		ownerRecvBuffer = inOwnerRecvBuffer;
		ownerSessionGeneration = inOwnerSessionGeneration;
		buffer = inBuffer;
		memcpy(clientAddrBuffer, inClientAddrBuffer, sizeof(SOCKADDR_INET));
	}

	RUDPSession* session{};
	RecvBuffer* ownerRecvBuffer{};
	uint32_t ownerSessionGeneration{};
	NetBuffer* buffer{};
	char clientAddrBuffer[sizeof(SOCKADDR_INET)];
};
