#include "PreCompile.h"
#include "RUDPSession.h"
#include <WinSock2.h>
#include "NetServerSerializeBuffer.h"

RUDPSession::RUDPSession(SessionIdType inSessionId, SOCKET inSock, PortType inServerPort)
	: sessionId(inSessionId)
	, sock(inSock)
	, serverPort(inServerPort)
{
}

std::shared_ptr<RUDPSession> RUDPSession::Create(SessionIdType inSessionId, SOCKET inSock, PortType inPort)
{
	struct RUDPSessionCreator : public RUDPSession
	{
		RUDPSessionCreator(SessionIdType inSessionId, SOCKET inSock, PortType inPort)
			: RUDPSession(inSessionId, inSock, inPort)
		{
		}
	};

	return std::make_shared<RUDPSessionCreator>(inSessionId, inSock, inPort);
}

bool RUDPSession::InitializeRIO(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable, RIO_CQ& rioRecvCQ, RIO_CQ& rioSendCQ)
{
	u_long nonBlocking = 1;
	ioctlsocket(sock, FIONBIO, &nonBlocking);

	recvBuffer.recvBufferId = rioFunctionTable.RIORegisterBuffer(recvBuffer.recvRingBuffer.GetBufferPtr(), DEFAULT_RINGBUFFER_MAX);
	if (recvBuffer.recvBufferId == RIO_INVALID_BUFFERID)
	{
		std::cout << "Recv RIORegisterBuffer failed" << std::endl;
		return false;
	}

	sendBuffer.sendBufferId = rioFunctionTable.RIORegisterBuffer(sendBuffer.rioSendBuffer, maxSendBufferSize);
	if (sendBuffer.sendBufferId == RIO_INVALID_BUFFERID)
	{
		std::cout << "Send RIORegisterBuffer failed" << std::endl;
		return false;
	}

	rioRQ = rioFunctionTable.RIOCreateRequestQueue(sock, 32, 1, 32, 1, rioRecvCQ, rioSendCQ, &sessionId);
	if (rioRQ == RIO_INVALID_RQ)
	{
		std::cout << "RIORegisterBuffer failed" << std::endl;
		return false;
	}

	return true;
}

RUDPSession::~RUDPSession()
{
	closesocket(sock);
}

void RUDPSession::OnRecv()
{
}
