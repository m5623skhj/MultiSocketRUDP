#include "PreCompile.h"
#include "RUDPSession.h"
#include "NetServerSerializeBuffer.h"
#include "MultiSocketRUDPCore.h"
#include "EssentialHandler.h"
#include "LogExtension.h"
#include "Logger.h"

RUDPSession::RUDPSession(SOCKET inSock, PortType inServerPort, MultiSocketRUDPCore& inCore)
	: sock(inSock)
	, serverPort(inServerPort)
	, core(inCore)
{
	ZeroMemory(recvBuffer.buffer, sizeof(recvBuffer.buffer));
	ZeroMemory(sendBuffer.rioSendBuffer, sizeof(sendBuffer.rioSendBuffer));
}

RUDPSession* RUDPSession::Create(SOCKET inSock, PortType inPort, MultiSocketRUDPCore& inCore)
{
	struct RUDPSessionCreator : public RUDPSession
	{
		RUDPSessionCreator(SOCKET inSock, PortType inPort, MultiSocketRUDPCore& inCore)
			: RUDPSession(inSock, inPort, inCore)
		{
		}
	};

	return new RUDPSessionCreator(inSock, inPort, inCore);
}

bool RUDPSession::InitializeRIO(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable, RIO_CQ& rioRecvCQ, RIO_CQ& rioSendCQ)
{
	u_long nonBlocking = 1;
	ioctlsocket(sock, FIONBIO, &nonBlocking);

	recvBuffer.recvBufferId = rioFunctionTable.RIORegisterBuffer(recvBuffer.buffer, dfDEFAULTSIZE);
	if (recvBuffer.recvBufferId == RIO_INVALID_BUFFERID)
	{
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = "Recv RIORegisterBuffer failed";
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	sendBuffer.sendBufferId = rioFunctionTable.RIORegisterBuffer(sendBuffer.rioSendBuffer, maxSendBufferSize);
	if (sendBuffer.sendBufferId == RIO_INVALID_BUFFERID)
	{
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = "Send RIORegisterBuffer failed";
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	rioRQ = rioFunctionTable.RIOCreateRequestQueue(sock, 32, 1, 32, 1, rioRecvCQ, rioSendCQ, &sessionId);
	if (rioRQ == RIO_INVALID_RQ)
	{
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = "RIORegisterBuffer failed with error code " + WSAGetLastError();
		Logger::GetInstance().WriteLog(log);
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
	bool connectState{ true };
	if (not isConnected.compare_exchange_strong(connectState, false))
	{
		return;
	}

	{
		std::unique_lock lock(sendPacketInfoMapLock);
		for (const auto& item : sendPacketInfoMap)
		{
			core.EraseSendPacketInfo(item.second, threadId);
		}
	}
	OnDisconnected();

	core.DisconnectSession(sessionId);
}

bool RUDPSession::SendPacket(IPacket& packet)
{
	NetBuffer* buffer = NetBuffer::Alloc();
	if (buffer == nullptr)
	{
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = "Buffer is nullptr in RUDPSession::SendPacket()";
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	PACKET_TYPE packetType = PACKET_TYPE::SendType;
	PacketSequence packetSequence = ++lastSendPacketSequence;
	*buffer << packetType << packetSequence << packet.GetPacketId();
	packet.PacketToBuffer(*buffer);

	return SendPacket(*buffer, packetSequence);
}

void RUDPSession::OnConnected(SessionIdType inSessionId)
{
	sessionId = inSessionId;
	EssentialHandlerManager::GetInst().CallRegisteredHandler(*this, EssentialHandlerManager::EssentialHandlerType::OnConnectedHandlerType);
}

void RUDPSession::OnDisconnected()
{
	EssentialHandlerManager::GetInst().CallRegisteredHandler(*this, EssentialHandlerManager::EssentialHandlerType::OnDisconnectedHandlerType);
}

bool RUDPSession::SendPacket(NetBuffer& buffer, const PacketSequence inSendPacketSequence)
{
	auto sendPacketInfo = sendPacketInfoPool->Alloc();
	if (sendPacketInfo == nullptr)
	{
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = "SendPacketInfo is nullptr in RUDPSession::SendPacket()";
		Logger::GetInstance().WriteLog(log);
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
	PacketSequence packetSequence;
	SessionIdType inputSessionId;
	std::string inputSessionKey;

	recvPacket >> packetSequence >> inputSessionId >> inputSessionKey;
	if (packetSequence != loginPacketSequence || sessionId != inputSessionId || sessionKey != inputSessionKey)
	{
		return;
	}

	bool connectState{ false };
	if (not isConnected.compare_exchange_strong(connectState, true))
	{
		return;
	}

	OnConnected(sessionId);
}

void RUDPSession::Disconnect(NetBuffer& recvPacket)
{
	Disconnect();
}

bool RUDPSession::OnRecvPacket(NetBuffer& recvPacket)
{
	PacketSequence packetSequece;
	recvPacket >> packetSequece;

	if (lastReceivedPacketSequence != packetSequece)
	{
		std::scoped_lock lock(recvPacketHolderQueueLock);

		NetBuffer::AddRefCount(&recvPacket);
		recvPacketHolderQueue.push(RecvPacketInfo{ &recvPacket, packetSequece });

		return false;
	}
	
	if (ProcessPacket(recvPacket) == false)
	{
		return false;
	}

	return ProcessHoldingPacket();
}

bool RUDPSession::ProcessHoldingPacket()
{
	PacketSequence packetSequece;
	size_t queueRestSize = 0;

	{
		std::scoped_lock lock(recvPacketHolderQueueLock);
		queueRestSize = recvPacketHolderQueue.size();
	}

	while (queueRestSize > 0)
	{
		NetBuffer* storedBuffer = nullptr;
		{
			std::scoped_lock lock(recvPacketHolderQueueLock);
			auto& recvPacketHolderTop = recvPacketHolderQueue.top();
			if (recvPacketHolderTop.packetSequence != lastSendPacketSequence)
			{
				break;
			}

			packetSequece = recvPacketHolderTop.packetSequence;
			storedBuffer = recvPacketHolderTop.buffer;
			recvPacketHolderQueue.pop();
		}

		if (ProcessPacket(*storedBuffer) == false)
		{
			return false;
		}
		--queueRestSize;
	}

	return true;
}

bool RUDPSession::ProcessPacket(NetBuffer& recvPacket)
{
	++lastReceivedPacketSequence;
	
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

SessionIdType RUDPSession::GetSessionId()
{
	return sessionId;
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