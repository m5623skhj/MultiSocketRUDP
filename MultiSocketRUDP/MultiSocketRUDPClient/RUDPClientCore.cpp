#include "PreCompile.h"
#include "RUDPClientCore.h"
#include "Logger.h"
#include "LogExtension.h"

RUDPClientCore& RUDPClientCore::GetInst()
{
	static RUDPClientCore instance;
	return instance;
}

bool RUDPClientCore::Start(const std::wstring& clientCoreOptionFile, const std::wstring& sessionGetterOptionFilePath, const bool printLogToConsole)
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
	constexpr PacketSequence packetSequence = 0;
	auto packetType = PACKET_TYPE::CONNECT_TYPE;
	
	connectPacket << packetType << packetSequence << sessionId << sessionKey;
	SendPacket(connectPacket, packetSequence);
}

void RUDPClientCore::RunThreads()
{
	sendEventHandles[0] = CreateSemaphore(nullptr, 0, LONG_MAX, nullptr);
	sendEventHandles[1] = CreateEvent(nullptr, TRUE, FALSE, nullptr);

	recvThread = std::jthread([this]() { this->RunRecvThread(); });
	sendThread = std::jthread([this]() { this->RunSendThread(); });
	retransmissionThread = std::jthread([this]() { this->RunRetransmissionThread(); });
}

void RUDPClientCore::RunRecvThread()
{
	NetBuffer* buffer = NetBuffer::Alloc();
	buffer->Resize(RECV_BUFFER_SIZE);
	sockaddr_in senderAddr{};
	int senderLength{sizeof(senderAddr)};

	while (not threadStopFlag)
	{
		buffer->Init();
		buffer->m_iWrite = 0;

		const int bytesReceived = recvfrom(rudpSocket, buffer->GetBufferPtr(), RECV_BUFFER_SIZE, 0, reinterpret_cast<sockaddr*>(&senderAddr), &senderLength);
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
		switch (WaitForMultipleObjects(static_cast<DWORD>(sendEventHandles.size()), sendEventHandles.data(), FALSE, INFINITE))
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

		for (const auto& sendPacketInfo : copyList | std::views::values)
		{
			if (sendPacketInfo->retransmissionTimeStamp > tickSet.nowTick)
			{
				continue;
			}

			if (++sendPacketInfo->retransmissionCount >= maxPacketRetransmissionCount)
			{
				auto log = Logger::MakeLogObject<ClientLog>();
				log->logString = "The maximum number of packet retransmission controls has been exceeded, and RUDPClientCore terminates";
				Logger::GetInstance().WriteLog(log);

				isConnected = false;
				Stop();
				break;
			}

			SendPacket(*sendPacketInfo);
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

		NetBuffer* recvPacketBuffer = NetBuffer::Alloc();
		recvBuffer.ReadBuffer(recvPacketBuffer->GetBufferPtr(), df_HEADER_SIZE);
		recvPacketBuffer->m_iRead = 0;

		WORD payloadLength = GetPayloadLength(*recvPacketBuffer);
		if (payloadLength <= 0 || payloadLength > dfDEFAULTSIZE || payloadLength > recvSize)
		{
			NetBuffer::Free(recvPacketBuffer);
			break;
		}
		const int packetSize = (payloadLength + df_HEADER_SIZE);
		recvSize -= packetSize;
		
		recvBuffer.ReadBuffer(recvPacketBuffer->GetWriteBufferPtr(), payloadLength);
		recvPacketBuffer->m_iWrite = static_cast<WORD>(packetSize);
		if (not recvPacketBuffer->Decode())
		{
			NetBuffer::Free(recvPacketBuffer);
			break;
		}

		ProcessRecvPacket(*recvPacketBuffer);
		NetBuffer::Free(recvPacketBuffer);
	}
}

void RUDPClientCore::ProcessRecvPacket(OUT NetBuffer& receivedBuffer)
{
	PACKET_TYPE packetType;
	PacketSequence packetSequence;
	receivedBuffer >> packetType >> packetSequence;

	switch (packetType)
	{
	case PACKET_TYPE::SEND_TYPE:
	case PACKET_TYPE::HEARTBEAT_TYPE:
	{
		NetBuffer::AddRefCount(&receivedBuffer);
		{
			std::scoped_lock lock(recvPacketHoldingQueueLock);
			recvPacketHoldingQueue.emplace(&receivedBuffer, packetSequence, packetType);
		}

		if (packetType == PACKET_TYPE::HEARTBEAT_TYPE)
		{
			SendReplyToServer(packetSequence, PACKET_TYPE::HEARTBEAT_REPLY_TYPE);
		}
		else
		{
			SendReplyToServer(packetSequence);
		}
		break;
	}
	case PACKET_TYPE::SEND_REPLY_TYPE:
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

	std::cout << "OnSendReply : " << packetSequence << '\n';
	if (packetSequence == 0)
	{
		isConnected = true;
	}

	{
		std::scoped_lock lock(sendPacketInfoMapLock);
		sendPacketInfoMap.erase(packetSequence);
	}
}

void RUDPClientCore::SendReplyToServer(const PacketSequence inRecvPacketSequence, const PACKET_TYPE packetType)
{
	auto& buffer = *NetBuffer::Alloc();

	buffer << packetType << inRecvPacketSequence;

	{
		std::scoped_lock lock(sendBufferQueueLock);
		sendBufferQueue.Enqueue(&buffer);

		ReleaseSemaphore(sendEventHandles[0], 1, nullptr);
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
		if (sendto(rudpSocket, packet->GetBufferPtr(), packet->GetAllUseSize(), 0, reinterpret_cast<const sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR)
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
	const UINT64 now = GetTickCount64();

	if (const UINT64 delta = now - tickSet.nowTick; delta < intervalMs)
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

		if (holdingPacketInfo.packetSequence < nextRecvPacketSequence)
		{
			recvPacketHoldingQueue.pop();
			continue;
		}
		else if (holdingPacketInfo.packetSequence != nextRecvPacketSequence)
		{
			return nullptr;
		}

		++nextRecvPacketSequence;
		recvPacketHoldingQueue.pop();
		if (holdingPacketInfo.packetType == PACKET_TYPE::HEARTBEAT_TYPE)
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

	PACKET_TYPE packetType = PACKET_TYPE::SEND_TYPE;
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
	if (sendPacketInfo->retransmissionCount == 0)
	{
		std::unique_lock lock(sendPacketInfoMapLock);
		sendPacketInfoMap.insert({ inSendPacketSequence, sendPacketInfo });
	}

	{
		std::scoped_lock lock(sendBufferQueueLock);
		sendBufferQueue.Enqueue(&buffer);
	}

	ReleaseSemaphore(sendEventHandles[0], 1, nullptr);
}

void RUDPClientCore::SendPacket(const SendPacketInfo& sendPacketInfo)
{
	{
		std::scoped_lock lock(sendBufferQueueLock);
		sendBufferQueue.Enqueue(sendPacketInfo.buffer);
	}
	ReleaseSemaphore(sendEventHandles[0], 1, nullptr);
}

WORD RUDPClientCore::GetPayloadLength(OUT const NetBuffer& buffer)
{
	static constexpr int PAYLOAD_LENGTH_POSITION = 1;

	return *reinterpret_cast<WORD*>(&buffer.m_pSerializeBuffer[buffer.m_iRead + PAYLOAD_LENGTH_POSITION]);
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

	const int iJumpBOM = ftell(fp);
	fseek(fp, 0, SEEK_END);
	const int iFileSize = ftell(fp);
	fseek(fp, iJumpBOM, SEEK_SET);
	const int fileSize = static_cast<int>(fread_s(cBuffer, BUFFER_MAX, sizeof(WCHAR), iFileSize / 2, fp));
	const int amend = iFileSize - fileSize;
	fclose(fp);

	cBuffer[iFileSize - amend] = '\0';
	WCHAR* pBuff = cBuffer;

	if (!parser.GetValue_Short(pBuff, L"CORE", L"MAX_PACKET_RETRANSMISSION_COUNT", reinterpret_cast<short*>(&maxPacketRetransmissionCount)))
		return false;
	if (!parser.GetValue_Int(pBuff, L"CORE", L"RETRANSMISSION_MS", reinterpret_cast<int*>(&retransmissionThreadSleepMs)))
		return false;

	return true;
}