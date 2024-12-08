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
	lastSendPacketSequence = {};
	sendPacketInfoMap.clear();
}

RUDPSession::~RUDPSession()
{
	closesocket(sock);
}

void RUDPSession::Disconnect()
{
	core.DisconnectSession(sessionId);
}

bool RUDPSession::SendPacket(IPacket& packet)
{
	NetBuffer* buffer = NetBuffer::Alloc();
	if (buffer == nullptr)
	{
		std::cout << "Buffer is nullptr in RUDPSession::SendPacket()" << std::endl;
		return false;
	}

	PACKET_TYPE packetType = PACKET_TYPE::SendType;
	unsigned long long packetSequence = ++lastSendPacketSequence;
	*buffer << packetType << packetSequence << packet.GetPacketId();
	packet.PacketToBuffer(*buffer);

	return SendPacket(*buffer, packetSequence);
}

void RUDPSession::OnConnected(SessionIdType sessionId)
{

}

void RUDPSession::OnDisconnected()
{

}

bool RUDPSession::SendPacket(NetBuffer& buffer, const PacketSequence inSendPacketSequence)
{
	auto sendPacketInfo = sendPacketInfoPool->Alloc();
	if (sendPacketInfo == nullptr)
	{
		std::cout << "SendPacketInfo is nullptr in RUDPSession::SendPacket()" << std::endl;
		NetBuffer::Free(&buffer);
		return false;
	}

	sendPacketInfo->Initialize(this, &buffer, inSendPacketSequence);
	{
		std::unique_lock lock(sendPacketInfoMapLock);
		sendPacketInfoMap.insert({ inSendPacketSequence, sendPacketInfo });
	}

	if (not core.SendPacket(sendPacketInfo))
	{
		std::unique_lock lock(sendPacketInfoMapLock);
		sendPacketInfoMap.erase(inSendPacketSequence);

		return false;
	}

	return true;
}

void RUDPSession::TryConnect(NetBuffer& recvPacket)
{
	if (isConnected)
	{
		return;
	}

	PacketSequence packetSequence;
	SessionIdType inputSessionId;
	std::string inputSessionKey;

	recvPacket >> packetSequence >> inputSessionId >> inputSessionKey;
	if (packetSequence != loginPacketSequence || sessionId != inputSessionId || sessionKey != inputSessionKey)
	{
		return;
	}

	OnConnected(sessionId);
	isConnected = true;
}

void RUDPSession::Disconnect(NetBuffer& recvPacket)
{
	isConnected = false;
	OnDisconnected();
}

bool RUDPSession::OnRecvPacket(NetBuffer& recvPacket)
{
	PacketSequence packetSequece;
	recvPacket >> packetSequece;

	if (lastReceivedPacketSequence != packetSequece)
	{
		std::unique_lock lock(recvPacketHolderQueueLock);

		NetBuffer::AddRefCount(&recvPacket);
		recvPacketHolderQueue.push(RecvPacketInfo{ &recvPacket, packetSequece });

		return false;
	}
	
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

void RUDPSession::OnSendReply(NetBuffer& recvPacket)
{
	PacketSequence packetSequence;
	recvPacket >> packetSequence;

	if (lastSendPacketSequence < packetSequence)
	{
		return;
	}

	SendPacketInfo* sendedPacketInfo = nullptr;
	{
		std::unique_lock lock(sendPacketInfoMapLock);
		auto itor = sendPacketInfoMap.find(packetSequence);
		if (itor != sendPacketInfoMap.end())
		{
			sendedPacketInfo = itor->second;
		}

		sendPacketInfoMap.erase(packetSequence);
	}

	core.EraseSendPacketInfo(sendedPacketInfo, threadId);
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