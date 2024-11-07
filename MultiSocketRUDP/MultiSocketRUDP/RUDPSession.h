#pragma once
#include "CoreType.h"
#include <MSWSock.h>
#include "LockFreeQueue.h"
#include "NetServerSerializeBuffer.h"

class MultiSocketRUDPCore;

enum class IO_MODE : LONG
{
	IO_NONE_SENDING = 0
	, IO_SENDING
};

struct RecvBuffer
{
	char buffer[recvBufferSize];
	RIO_BUFFERID recvBufferId;
};

struct SendBuffer
{
	WORD bufferCount = 0;
	CLockFreeQueue<NetBuffer*> sendQueue;
	char rioSendBuffer[maxSendBufferSize];
	RIO_BUFFERID sendBufferId;
	IO_MODE ioMode = IO_MODE::IO_NONE_SENDING;
};

class RUDPSession
{
	friend MultiSocketRUDPCore;

private:
	RUDPSession() = delete;
	explicit RUDPSession(SessionIdType inSessionId, SOCKET inSock, PortType inServerPort);

	static std::shared_ptr<RUDPSession> Create(SessionIdType inSessionId, SOCKET inSock, PortType inPort);
	bool InitializeRIO(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable, RIO_CQ& rioRecvCQ, RIO_CQ& rioSendCQ);
	void InitializeSession();

public:
	virtual ~RUDPSession();

private:
	bool OnConnect(NetBuffer& recvPacket);
	void OnDisconnect(NetBuffer& recvPacket);
	bool OnRecvPacket(NetBuffer& recvPacket);

private:
	SessionIdType sessionId;
	// a connectKey seems to be necessary
	// generate and store a key on the TCP connection side,
	// then insert the generated key into the packet and send it
	// if the connectKey matches, verifying it as a valid key,
	// insert the client information into clientAddr below
	std::string sessionKey{};
	sockaddr_in clientAddr{};
	PortType serverPort;
	SOCKET sock;
	bool isUsingSession{};
	bool ioCancle{};

private:
	RecvBuffer recvBuffer;
	SendBuffer sendBuffer;

	RIO_RQ rioRQ = RIO_INVALID_RQ;
};
