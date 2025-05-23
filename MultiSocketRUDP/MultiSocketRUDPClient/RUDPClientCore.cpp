#include "PreCompile.h"
#include "RUDPClientCore.h"
#include "Logger.h"
#include "LogExtention.h"

RUDPClientCore& RUDPClientCore::GetInst()
{
	static RUDPClientCore instance;
	return instance;
}

bool RUDPClientCore::Start(const std::wstring& clientCoreOptionFile, const std::wstring& sessionGetterOptionFilePath, bool printLogToConsole)
{
	Logger::GetInstance().RunLoggerThread(printLogToConsole);

	if (not ReadOptionFile(clientCoreOptionFile, sessionGetterOptionFilePath))
	{
		return false;
	}

#if USE_IOCP_SESSION_GETTER
	if (not sessionGetter.Start(optionFilePath))
	{
		return false;
	}
#else
	if (not RunGetSessionFromServer(sessionGetterOptionFilePath))
	{
		return false;
	}
#endif

	if (not CreateRUDPSocket())
	{
		return false;
	}
	RunThreads();
	Sleep(1000);

	SendConnectPacket();

	return true;
}

void RUDPClientCore::Stop()
{
	closesocket(sessionBrokerSocket);
	closesocket(rudpSocket);

	SetEvent(sendEventHandles[1]);
	threadStopFlag = true;

	const std::thread::id nowThreadId = std::this_thread::get_id();
	retransmissionThread.join();
	sendThread.join();
	recvThread.join();
	Logger::GetInstance().StopLoggerThread();

	isStopped = true;
}

bool RUDPClientCore::CreateRUDPSocket()
{
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(port);
	InetPtonA(AF_INET, serverIp.c_str(), &serverAddr.sin_addr);

	if (rudpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); rudpSocket == INVALID_SOCKET)
	{
		auto log = Logger::MakeLogObject<ClientLog>();
		log->logString = std::format("socket() failed with error {}", WSAGetLastError());
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	sockaddr_in recvAddr;
	recvAddr.sin_family = AF_INET;
	recvAddr.sin_addr.s_addr = INADDR_ANY;
	recvAddr.sin_port = 0;

	if (bind(rudpSocket, reinterpret_cast<sockaddr*>(&recvAddr), sizeof(recvAddr)) == SOCKET_ERROR)
	{
		auto log = Logger::MakeLogObject<ClientLog>();
		log->logString = std::format("bind() failed with error {}", WSAGetLastError());
		Logger::GetInstance().WriteLog(log);
		closesocket(rudpSocket);
		return false;
	}

	return true;
}

void RUDPClientCore::SendConnectPacket()
{
	NetBuffer& connectPacket = *NetBuffer::Alloc();
	PacketSequence packetSequence = 0;
	PACKET_TYPE packetType = PACKET_TYPE::ConnectType;
	
	connectPacket << packetType << packetSequence << sessionId << sessionKey;
	SendPacket(connectPacket, packetSequence);
}

void RUDPClientCore::RunThreads()
{
	sendEventHandles[0] = CreateEvent(NULL, FALSE, FALSE, NULL);
	sendEventHandles[1] = CreateEvent(NULL, TRUE, FALSE, NULL);

	recvThread = std::jthread([this]() { this->RunRecvThread(); });
	sendThread = std::jthread([this]() { this->RunSendThread(); });
	retransmissionThread = std::jthread([this]() { this->RunRetransmissionThread(); });
}

void RUDPClientCore::RunRecvThread()
{
	NetBuffer* buffer = NetBuffer::Alloc();
	buffer->Resize(recvBufferSize);
	sockaddr_in senderAddr{};
	int senderLength{sizeof(senderAddr)};

	while (not threadStopFlag)
	{
		buffer->Init();
		buffer->m_iWrite = 0;
		
		int bytesReceived = recvfrom(rudpSocket, buffer->GetBufferPtr(), recvBufferSize, 0, (sockaddr*)&senderAddr, &senderLength);
		if (bytesReceived == SOCKET_ERROR)
		{
			const int error = WSAGetLastError();
			
			auto log = Logger::MakeLogObject<ClientLog>();
			if (error == WSAENOTSOCK || error == WSAEINTR)
			{
				log->logString = "Recv thread stopped";
			}
			else
			{
				log->logString = std::format("recvfrom() error with {}", error);
			}

			Logger::GetInstance().WriteLog(log);
			continue;
		}
		buffer->MoveWritePos(bytesReceived);
		buffer->m_iRead = 0;
		OnRecvStream(*buffer, bytesReceived);
	}

	NetBuffer::Free(buffer);
}

void RUDPClientCore::RunSendThread()
{
	while (true)
	{
		const auto waitResult = WaitForMultipleObjects(static_cast<DWORD>(sendEventHandles.size()), sendEventHandles.data(), FALSE, INFINITE);
		switch (waitResult)
		{
		case WAIT_OBJECT_0:
		{
			DoSend();
		}
		break;
		case WAIT_OBJECT_0 + 1:
		{
			DoSend();
			auto log = Logger::MakeLogObject<ClientLog>();
			log->logString = "Send thread stopped";
			Logger::GetInstance().WriteLog(log);
			return;
		}
		break;
		default:
		{
			auto log = Logger::MakeLogObject<ClientLog>();
			log->logString = std::format("Invalid send thread wait result. Error is {}", WSAGetLastError());
			Logger::GetInstance().WriteLog(log);
			g_Dump.Crash();
		}
		break;
		}
	}
}

void RUDPClientCore::RunRetransmissionThread()
{
	TickSet tickSet;
	tickSet.nowTick = GetTickCount64();

	std::list<std::pair<PacketSequence, SendPacketInfo*>> copyList;
	while (not threadStopFlag)
	{
		{
			std::scoped_lock lock(sendPacketInfoMapLock);
			copyList.assign(sendPacketInfoMap.begin(), sendPacketInfoMap.end());
		}

		for (auto& sendedPacketInfo : copyList)
		{
			if (sendedPacketInfo.second->retransmissionTimeStamp > tickSet.nowTick)
			{
				continue;
			}

			if (++sendedPacketInfo.second->retransmissionCount >= maxPacketRetransmissionCount)
			{
				auto log = Logger::MakeLogObject<ClientLog>();
				log->logString = "The maximum number of packet retransmission controls has been exceeded, and RUDPClientCore terminates";
				Logger::GetInstance().WriteLog(log);

				isConnected = false;
				Stop();
				break;
			}

			SendPacket(*sendedPacketInfo.second->GetBuffer(), sendedPacketInfo.second->sendPacektSequence);
		}

		SleepRemainingFrameTime(tickSet, retransmissionThreadSleepMs);
	}

	auto log = Logger::MakeLogObject<ClientLog>();
	log->logString = "Retransmission thread stopped";
	Logger::GetInstance().WriteLog(log);
}

void RUDPClientCore::OnRecvStream(NetBuffer& recvBuffer, int recvSize)
{
	NetBuffer* packetBuffer = nullptr;

	while (recvSize > df_HEADER_SIZE)
	{
		if (packetBuffer != nullptr)
		{
			NetBuffer::Free(packetBuffer);
			packetBuffer = nullptr;
		}

		NetBuffer* packetBuffer = NetBuffer::Alloc();
		recvBuffer.ReadBuffer(packetBuffer->GetBufferPtr(), df_HEADER_SIZE);
		packetBuffer->m_iRead = 0;

		WORD payloadLength = GetPayloadLength(*packetBuffer);
		if (payloadLength <= 0 || payloadLength > dfDEFAULTSIZE || payloadLength > recvSize)
		{
			NetBuffer::Free(packetBuffer);
			break;
		}
		int packetSize = (payloadLength + df_HEADER_SIZE);
		recvSize -= packetSize;
		
		recvBuffer.ReadBuffer(packetBuffer->GetWriteBufferPtr(), payloadLength);
		packetBuffer->m_iWrite = packetSize;
		if (not packetBuffer->Decode())
		{
			NetBuffer::Free(packetBuffer);
			break;
		}

		ProcessRecvPacket(*packetBuffer);
		NetBuffer::Free(packetBuffer);
	}
}

void RUDPClientCore::ProcessRecvPacket(OUT NetBuffer& receivedBuffer)
{
	PACKET_TYPE packetType;
	PacketSequence packetSequence;
	receivedBuffer >> packetType >> packetSequence;

	switch (packetType)
	{
	case PACKET_TYPE::SendType:
	case PACKET_TYPE::HeartbeatType:
	{
		NetBuffer::AddRefCount(&receivedBuffer);
		{
			std::scoped_lock lock(recvPacketHoldingQueueLock);
			recvPacketHoldingQueue.emplace(RecvPacketInfo{ &receivedBuffer, packetSequence, packetType });
		}

		if (packetType == PACKET_TYPE::HeartbeatType)
		{
			SendReplyToServer(packetSequence);
		}
		else
		{
			SendReplyToServer(packetSequence, PACKET_TYPE::HeartbeatReplyType);
		}
		SendReplyToServer(packetSequence);
		break;
	}
	case PACKET_TYPE::SendReplyType:
	{
		OnSendReply(receivedBuffer, packetSequence);
		break;
	}
	default:
	{
		auto log = Logger::MakeLogObject<ClientLog>();
		log->logString = std::format("Invalid packet type {}", static_cast<unsigned char>(packetType));
		Logger::GetInstance().WriteLog(log);
		break;
	}
	}
}

void RUDPClientCore::OnSendReply(NetBuffer& recvPacket, const PacketSequence packetSequence)
{
	if (lastSendPacketSequence < packetSequence)
	{
		return;
	}

	if (packetSequence == 0)
	{
		isConnected = true;
	}

	{
		std::scoped_lock lock(sendPacketInfoMapLock);
		sendPacketInfoMap.erase(packetSequence);
	}
}

void RUDPClientCore::SendReplyToServer(const PacketSequence recvPacketSequence, const PACKET_TYPE packetType)
{
	auto& buffer = *NetBuffer::Alloc();

	buffer << packetType << recvPacketSequence;

	{
		std::scoped_lock lock(sendBufferQueueLock);
		sendBufferQueue.Enqueue(&buffer);

		SetEvent(sendEventHandles[0]);
	}
}

void RUDPClientCore::DoSend()
{
	while (sendBufferQueue.GetRestSize() > 0)
	{
		NetBuffer* packet = nullptr;
		if (not sendBufferQueue.Dequeue(&packet))
		{
			auto log = Logger::MakeLogObject<ClientLog>();
			log->logString = "sendBufferQueue.Dequeue() failed";
			Logger::GetInstance().WriteLog(log);
			continue;
		}

		EncodePacket(*packet);
		if (sendto(rudpSocket, packet->GetBufferPtr(), packet->GetAllUseSize(), 0, (const sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
		{
			auto log = Logger::MakeLogObject<ClientLog>();
			log->logString = std::format("sendto() failed with error code {}", WSAGetLastError());
			Logger::GetInstance().WriteLog(log);
			continue;
		}
	}
}

void RUDPClientCore::SleepRemainingFrameTime(OUT TickSet& tickSet, const unsigned int intervalMs)
{
	UINT64 now = GetTickCount64();
	UINT64 delta = now - tickSet.nowTick;

	if (delta < intervalMs)
	{
		Sleep(static_cast<DWORD>(intervalMs - delta));
	}

	tickSet.nowTick = GetTickCount64();
}

unsigned int RUDPClientCore::GetRemainPacketSize()
{
	std::scoped_lock lock(recvPacketHoldingQueueLock);
	return static_cast<unsigned int>(recvPacketHoldingQueue.size());
}

NetBuffer* RUDPClientCore::GetReceivedPacket()
{
	std::scoped_lock lock(recvPacketHoldingQueueLock);
	while (not recvPacketHoldingQueue.empty())
	{
		auto holdingPacketInfo = recvPacketHoldingQueue.top();

		if (holdingPacketInfo.packetSequence <= recvPacketSequence)
		{
			recvPacketHoldingQueue.pop();
			continue;
		}
		else if (holdingPacketInfo.packetSequence != recvPacketSequence + 1)
		{
			return nullptr;
		}

		++recvPacketSequence;
		recvPacketHoldingQueue.pop();
		if (holdingPacketInfo.packetType == PACKET_TYPE::HeartbeatType)
		{
			continue;
		}

		return holdingPacketInfo.buffer;
	}

	return nullptr;
}

void RUDPClientCore::SendPacket(OUT IPacket& packet)
{
	NetBuffer* buffer = NetBuffer::Alloc();
	if (buffer == nullptr)
	{
		auto log = Logger::MakeLogObject<ClientLog>();
		log->logString = "Buffer is nullptr in RUDPSession::SendPacket()";
		Logger::GetInstance().WriteLog(log);
		return;
	}

	PACKET_TYPE packetType = PACKET_TYPE::SendType;
	PacketSequence packetSequence = ++lastSendPacketSequence;
	*buffer << packetType << packetSequence << packet.GetPacketId();
	packet.PacketToBuffer(*buffer);

	return SendPacket(*buffer, packetSequence);
}

void RUDPClientCore::SendPacket(OUT NetBuffer& buffer, const PacketSequence inSendPacketSequence)
{
	auto sendPacketInfo = sendPacketInfoPool->Alloc();
	if (sendPacketInfo == nullptr)
	{
		auto log = Logger::MakeLogObject<ClientLog>();
		log->logString = "SendPacketInfo is nullptr in RUDPSession::SendPacket()";
		Logger::GetInstance().WriteLog(log);
		NetBuffer::Free(&buffer);
		return;
	}

	sendPacketInfo->Initialize(&buffer, inSendPacketSequence);
	sendPacketInfo->retransmissionTimeStamp = GetTickCount64() + retransmissionThreadSleepMs;
	{
		std::unique_lock lock(sendPacketInfoMapLock);
		sendPacketInfoMap.insert({ inSendPacketSequence, sendPacketInfo });
	}

	{
		std::scoped_lock lock(sendBufferQueueLock);
		sendBufferQueue.Enqueue(&buffer);
	}

	SetEvent(sendEventHandles[0]);
}

WORD RUDPClientCore::GetPayloadLength(OUT NetBuffer& buffer) const
{
	static constexpr int payloadLengthPosition = 1;

	return *((WORD*)(&buffer.m_pSerializeBuffer[buffer.m_iRead + payloadLengthPosition]));
}

void RUDPClientCore::EncodePacket(OUT NetBuffer& packet)
{
	if (packet.m_bIsEncoded == false)
	{
		packet.m_iWriteLast = packet.m_iWrite;
		packet.m_iWrite = 0;
		packet.m_iRead = 0;
		packet.Encode();
	}
}

bool RUDPClientCore::ReadOptionFile(const std::wstring& clientCoreOptionFile, const std::wstring& sessionGetterOptionFilePath)
{
	if (not ReadClientCoreOptionFile(clientCoreOptionFile))
	{
		auto log = Logger::MakeLogObject<ClientLog>();
		log->logString = "RUDPClientCore::ReadClientCoreOptionFile() failed";
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	if (not ReadSessionGetterOptionFile(sessionGetterOptionFilePath))
	{
		auto log = Logger::MakeLogObject<ClientLog>();
		log->logString = "RunGetSessionFromServer::ReadOptionFile() failed";
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	return true;
}

bool RUDPClientCore::ReadClientCoreOptionFile(const std::wstring& optionFilePath)
{
	_wsetlocale(LC_ALL, L"Korean");

	CParser parser;
	WCHAR cBuffer[BUFFER_MAX];

	FILE* fp;
	_wfopen_s(&fp, optionFilePath.c_str(), L"rt, ccs=UNICODE");

	int iJumpBOM = ftell(fp);
	fseek(fp, 0, SEEK_END);
	int iFileSize = ftell(fp);
	fseek(fp, iJumpBOM, SEEK_SET);
	int FileSize = (int)fread_s(cBuffer, BUFFER_MAX, sizeof(WCHAR), iFileSize / 2, fp);
	int iAmend = iFileSize - FileSize;
	fclose(fp);

	cBuffer[iFileSize - iAmend] = '\0';
	WCHAR* pBuff = cBuffer;

	if (!parser.GetValue_Short(pBuff, L"CORE", L"MAX_PACKET_RETRANSMISSION_COUNT", (short*)&maxPacketRetransmissionCount))
		return false;
	if (!parser.GetValue_Int(pBuff, L"CORE", L"RETRANSMISSION_MS", (int*)&retransmissionThreadSleepMs))
		return false;

	return true;
}