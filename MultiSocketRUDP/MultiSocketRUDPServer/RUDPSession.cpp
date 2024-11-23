#include "PreCompile.h"
#include "RUDPSession.h"
#include "NetServerSerializeBuffer.h"
#include "PacketManager.h"
#include "MultiSocketRUDPCore.h"

RUDPSession::RUDPSession(SOCKET inSock, PortType inServerPort, MultiSocketRUDPCore& inCore)
	: sock(inSock)
	, serverPort(inServerPort)
	, core(inCore)
{
	ZeroMemory(recvBuffer.buffer, sizeof(recvBuffer.buffer));
	ZeroMemory(sendBuffer.rioSendBuffer, sizeof(sendBuffer.rioSendBuffer));
}

std::shared_ptr<RUDPSession> RUDPSession::Create(SOCKET inSock, PortType inPort, MultiSocketRUDPCore& inCore)
{
	struct RUDPSessionCreator : public RUDPSession
	{
		RUDPSessionCreator(SOCKET inSock, PortType inPort, MultiSocketRUDPCore& inCore)
			: RUDPSession(inSock, inPort, inCore)
		{
		}
	};

	return std::make_shared<RUDPSessionCreator>(inSock, inPort, inCore);
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
	sessionId = invalidSessionId;
	isConnected = {};
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

void RUDPSession::Disconnect()
{
	core.DisconnectSession(sessionId);
}

void RUDPSession::SendPacket(IPacket& packet)
{
	NetBuffer* buffer = NetBuffer::Alloc();
	if (buffer == nullptr)
	{
		std::cout << "Buffer is nullptr in RUDPSession::SendPacket()" << std::endl;
		return;
	}

	PACKET_TYPE packetType = PACKET_TYPE::SendType;
	unsigned long long packetSequence = ++lastSendPacketSequence;
	*buffer << packetType << packetSequence << packet.GetPacketId();
	packet.PacketToBuffer(*buffer);

	SendPacket(*buffer);
}

void RUDPSession::OnConnected(SessionIdType sessionId)
{

}

void RUDPSession::OnDisconnected()
{

}

void RUDPSession::SendPacket(NetBuffer& buffer)
{
	auto sendPacketInfo = sendPacketInfoPool->Alloc();
	if (sendPacketInfo == nullptr)
	{
		std::cout << "SendPacketInfo is nullptr in RUDPSession::SendPacket()" << std::endl;
		NetBuffer::Free(&buffer);
		return;
	}

	sendPacketInfo->Initialize(this, &buffer);
	core.SendPacket(sendPacketInfo);
}

void RUDPSession::TryConnect(NetBuffer& recvPacket)
{
	if (isConnected)
	{
		return;
	}

	SessionIdType inputSessionId;
	std::string inputSessionKey;

	recvPacket >> inputSessionId >> inputSessionKey;
	if (sessionId != inputSessionId || sessionKey != inputSessionKey)
	{
		return;
	}

	OnConnected(sessionId);
	isConnected = true;
}

bool RUDPSession::TryDisconnect(NetBuffer& recvPacket)
{
	OnDisconnected();

	return true;
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

	std::any realPacket = std::any(packet.get());
	if (not PacketManager::GetInst().BufferToPacket(packetId, recvPacket, realPacket))
	{
		return false;
	}

	return packetHandler(*this, (IPacket&)realPacket);
}

sockaddr_in RUDPSession::GetSocketAddress()
{
	return clientAddr;
}

bool RUDPSession::CheckMyClient(const sockaddr_in& targetClientAddr)
{
	if (clientAddr.sin_addr.S_un.S_addr != targetClientAddr.sin_addr.S_un.S_addr ||
		clientAddr.sin_port != targetClientAddr.sin_port)
	{
		return false;
	}

	return true;
}