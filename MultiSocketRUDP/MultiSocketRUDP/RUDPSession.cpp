#include "PreCompile.h"
#include "RUDPSession.h"
#include "NetServerSerializeBuffer.h"
#include "PacketManager.h"
#include "Protocol.h"

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

	recvBuffer.recvBufferId = rioFunctionTable.RIORegisterBuffer(recvBuffer.buffer, dfDEFAULTSIZE);
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

void RUDPSession::InitializeSession()
{
	sessionKey = {};
	clientAddr = {};
	isUsingSession = {};
	ioCancle = {};
	
	for (int i = 0; i < sendBuffer.sendQueue.GetRestSize(); ++i)
	{
		NetBuffer* eraseBuffer = nullptr;
		if (sendBuffer.sendQueue.Dequeue(&eraseBuffer))
		{
			NetBuffer::Free(eraseBuffer);
		}
	}
}

RUDPSession::~RUDPSession()
{
	closesocket(sock);
}

bool RUDPSession::OnConnect(NetBuffer& recvPacket)
{


	return true;
}

void RUDPSession::OnDisconnect(NetBuffer& recvPacket)
{

}

bool RUDPSession::OnRecvPacket(NetBuffer& recvPacket)
{
	PacketId packetId;
	recvPacket >> packetId;

	auto packetHandler = PacketManager::GetInst().GetPacketHandler(packetId);
	if (packetHandler == nullptr)
	{
		return false;
	}

	auto packet = PacketManager::GetInst().MakePacket(packetId);
	if (packet == nullptr)
	{
		return false;
	}

	char* targetPtr = reinterpret_cast<char*>(packet.get()) + sizeof(char*);
	std::any anyPacket = std::any(packet.get());
	return packetHandler(*this, recvPacket, anyPacket);
}
