#include "PreCompile.h"
#include "RUDPSession.h"
#include "NetServerSerializeBuffer.h"
#include "MultiSocketRUDPCore.h"
#include "EssentialHandler.h"
#include "LogExtension.h"
#include "Logger.h"

RUDPSession::RUDPSession(MultiSocketRUDPCore& inCore)
	: sock(INVALID_SOCKET)
	, serverPort(invalidPortNumber)
	, core(inCore)
{
	ZeroMemory(recvBuffer.buffer, sizeof(recvBuffer.buffer));
	ZeroMemory(sendBuffer.rioSendBuffer, sizeof(sendBuffer.rioSendBuffer));
}

bool RUDPSession::InitializeRIO(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable, RIO_CQ& rioRecvCQ, RIO_CQ& rioSendCQ)
{
	u_long nonBlocking = 1;
	ioctlsocket(sock, FIONBIO, &nonBlocking);

	if (not InitRIOSendBuffer(rioFunctionTable) || not InitRIORecvBuffer(rioFunctionTable))
	{
		return false;
	}

	rioRQ = rioFunctionTable.RIOCreateRequestQueue(sock, maxOutStandingReceive, 1, maxOutStandingSend, 1, rioRecvCQ, rioSendCQ, &sessionId);
	if (rioRQ == RIO_INVALID_RQ)
	{
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = std::format("RIOCreateRQ failed with {}", WSAGetLastError());
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	return true;
}

bool RUDPSession::InitRIOSendBuffer(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable)
{
	sendBuffer.sendBufferId = rioFunctionTable.RIORegisterBuffer(sendBuffer.rioSendBuffer, maxSendBufferSize);
	if (sendBuffer.sendBufferId == RIO_INVALID_BUFFERID)
	{
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = std::format("Send RIORegisterBuffer failed with {}", WSAGetLastError());
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	return true;
}

bool RUDPSession::InitRIORecvBuffer(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable)
{
	recvBuffer.recvContext = std::make_shared<IOContext>();
	if (recvBuffer.recvContext == nullptr)
	{
		return false;
	}

	auto& context = recvBuffer.recvContext;
	context->InitContext(sessionId, RIO_OPERATION_TYPE::OP_RECV);
	context->Length = recvBufferSize;
	context->Offset = 0;
	context->session = this;

	context->clientAddrRIOBuffer.Length = sizeof(SOCKADDR_INET);
	context->clientAddrRIOBuffer.Offset = 0;

	context->localAddrRIOBuffer.Length = sizeof(SOCKADDR_INET);
	context->localAddrRIOBuffer.Offset = 0;

	context->BufferId = rioFunctionTable.RIORegisterBuffer(recvBuffer.buffer, recvBufferSize);
	context->clientAddrRIOBuffer.BufferId = rioFunctionTable.RIORegisterBuffer(context->clientAddrBuffer, sizeof(SOCKADDR_INET));
	context->localAddrRIOBuffer.BufferId = rioFunctionTable.RIORegisterBuffer(context->localAddrBuffer, sizeof(SOCKADDR_INET));

	if (context->BufferId == RIO_INVALID_BUFFERID || context->clientAddrRIOBuffer.BufferId == RIO_INVALID_BUFFERID || context->localAddrRIOBuffer.BufferId == RIO_INVALID_BUFFERID)
	{
		return false;
	}

	return true;
}

void RUDPSession::InitializeSession()
{
	sessionId = invalidSessionId;
	sessionKey = {};
	clientAddr = {};
	clientSockaddrInet = {};
	lastSendPacketSequence = {};
	nowInReleaseThread = {};

	if (sendBuffer.reservedSendPacketInfo != nullptr)
	{
		sendPacketInfoPool->Free(sendBuffer.reservedSendPacketInfo);
		sendBuffer.reservedSendPacketInfo = {};
	}

	int sendPacketInfoQueueSize = sendBuffer.sendPacketInfoQueue.GetRestSize();
	SendPacketInfo* eraseTarget = nullptr;
	for (int i = 0; i < sendPacketInfoQueueSize; ++i)
	{
		if (sendBuffer.sendPacketInfoQueue.Dequeue(&eraseTarget))
		{
			sendPacketInfoPool->Free(eraseTarget);
		}
	}
}

RUDPSession::~RUDPSession()
{
	if (sock != INVALID_SOCKET)
	{
		closesocket(sock);
	}
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
	closesocket(sock);
	sock = INVALID_SOCKET;
	OnDisconnected();

	core.DisconnectSession(sessionId);
}

bool RUDPSession::SendPacket(IPacket& packet)
{
	if (not isConnected)
	{
		return false;
	}

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

	return SendPacket(*buffer, packetSequence, false);
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

bool RUDPSession::SendPacket(NetBuffer& buffer, const PacketSequence inSendPacketSequence, const bool isReplyType)
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

	sendPacketInfo->Initialize(this, &buffer, inSendPacketSequence, isReplyType);
	if (isReplyType == false)
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

void RUDPSession::SendHeartbeatPacket()
{
	NetBuffer& buffer = *NetBuffer::Alloc();

	PACKET_TYPE packetType = PACKET_TYPE::HeartbeatType;
	PacketSequence packetSequence = ++lastSendPacketSequence;
	std::cout << "HeartbeatPacketSequence : " << packetSequence << std::endl;
	buffer << packetType << packetSequence;

	std::ignore = SendPacket(buffer, packetSequence, false);
}

void RUDPSession::TryConnect(NetBuffer& recvPacket, const sockaddr_in& inClientAddr)
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
	clientAddr = inClientAddr;
	memset(&clientSockaddrInet, 0, sizeof(clientSockaddrInet));
	clientSockaddrInet.Ipv4 = inClientAddr;
	++lastReceivedPacketSequence;

	OnConnected(sessionId);
	SendReplyToClient(packetSequence);
}

void RUDPSession::Disconnect(NetBuffer& recvPacket)
{
	Disconnect();
}

bool RUDPSession::OnRecvPacket(NetBuffer& recvPacket)
{
	PacketSequence packetSequence;
	recvPacket >> packetSequence;
	std::cout << "SendType RecvPacketSequence : " << packetSequence << std::endl;
	if (lastReceivedPacketSequence != packetSequence)
	{
		NetBuffer::AddRefCount(&recvPacket);
		recvPacketHolderQueue.push(RecvPacketInfo{ &recvPacket, packetSequence });

		std::cout << "RecvPacketHolderQueueSequence : " << recvPacketHolderQueue.top().packetSequence << std::endl;

		SendReplyToClient(packetSequence);

		return true;
	}
	
	if (ProcessPacket(recvPacket, packetSequence) == false)
	{
		return false;
	}

	return ProcessHoldingPacket();
}

bool RUDPSession::ProcessHoldingPacket()
{
	PacketSequence packetSequence;

	while (not recvPacketHolderQueue.empty())
	{
		NetBuffer* storedBuffer = nullptr;
		{
			auto& recvPacketHolderTop = recvPacketHolderQueue.top();
			if (recvPacketHolderTop.packetSequence <= lastReceivedPacketSequence)
			{
				recvPacketHolderQueue.pop();
				continue;
			}
			else if (recvPacketHolderTop.packetSequence != lastReceivedPacketSequence + 1)
			{
				break;
			}
			
			packetSequence = recvPacketHolderTop.packetSequence;
			storedBuffer = recvPacketHolderTop.buffer;
			recvPacketHolderQueue.pop();
		}
		
		if (ProcessPacket(*storedBuffer, packetSequence, false) == false)
		{
			return false;
		}
	}
	
	return true;
}

bool RUDPSession::ProcessPacket(NetBuffer& recvPacket, const PacketSequence recvPacketSequence, const bool needReplyToClient)
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

	bool packetHandleResult = packetHandler(*this, *packet);
	if (packetHandleResult == true && needReplyToClient == true)
	{
		SendReplyToClient(recvPacketSequence);
	}

	return packetHandleResult;
}

void RUDPSession::SendReplyToClient(const PacketSequence recvPacketSequence)
{
	NetBuffer& buffer = *NetBuffer::Alloc();

	PACKET_TYPE packetType = PACKET_TYPE::SendReplyType;
	buffer << packetType << recvPacketSequence;

	std::ignore = SendPacket(buffer, recvPacketSequence, true);
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
		else
		{
			return;
		}

		sendPacketInfoMap.erase(packetSequence);
	}

	core.EraseSendPacketInfo(sendedPacketInfo, threadId);
	nowInProcessingRecvPacket = true;
	if (not ProcessHoldingPacket())
	{
		Disconnect();
	}

	nowInProcessingRecvPacket = false;
}

SessionIdType RUDPSession::GetSessionId() const
{
	return sessionId;
}

sockaddr_in RUDPSession::GetSocketAddress() const
{
	return clientAddr;
}

SOCKADDR_INET RUDPSession::GetSocketAddressInet() const
{
	return clientSockaddrInet;
}

bool RUDPSession::IsConnected() const
{
	return isConnected;
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

bool RUDPSession::IsReleasing() const
{
	return nowInReleaseThread;
}