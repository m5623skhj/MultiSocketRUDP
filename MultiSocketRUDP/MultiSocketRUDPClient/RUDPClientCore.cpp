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

	if (not ConnectToServer(clientCoreOptionFile))
	{
		return false;
	}
	RunThreads();

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

	isStopped = true;
}

bool RUDPClientCore::IsStopped()
{
	return isStopped;
}

bool RUDPClientCore::IsConnected()
{
	return isConnected;
}

bool RUDPClientCore::ConnectToServer(const std::wstring& optionFilePath)
{

	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(port);
	InetPtonA(AF_INET, serverIp.c_str(), &serverAddr.sin_addr);

	if (bind(rudpSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
	{
		auto log = Logger::MakeLogObject<ClientLog>();
		log->logString = "bind() failed with error " + WSAGetLastError();
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	NetBuffer& connectPacket = *NetBuffer::Alloc();
	PACKET_TYPE packetType = PACKET_TYPE::ConnectType;
	
	connectPacket << packetType << sessionId << sessionKey;
	SendPacket(connectPacket, ++lastSendPacketSequence);

	return true;
}

void RUDPClientCore::RunThreads()
{
	sendEventHandles[0] = CreateEvent(NULL, FALSE, FALSE, NULL);
	sendEventHandles[1] = CreateEvent(NULL, TRUE, FALSE, NULL);

	recvThread = std::thread([this]() { this->RunRecvThread(); });
	sendThread = std::thread([this]() { this->RunSendThread(); });
	retransmissionThread = std::thread([this]() { this->RunRetransmissionThread(); });
}

void RUDPClientCore::RunRecvThread()
{
	int senderLength = sizeof(serverAddr);
	NetBuffer* buffer = nullptr;
	while (threadStopFlag == true)
	{
		if (buffer != nullptr)
		{
			NetBuffer::Free(buffer);
		}
		buffer = NetBuffer::Alloc();

		int bytesReceived = recvfrom(rudpSocket, buffer->GetBufferPtr(), dfDEFAULTSIZE, 0, (sockaddr*)&serverAddr, &senderLength);
		if (bytesReceived == SOCKET_ERROR)
		{
			const int error = WSAGetLastError();
			NetBuffer::Free(buffer);
			
			if (error == WSAENOTSOCK || error == WSAEINTR)
			{
				auto log = Logger::MakeLogObject<ClientLog>();
				log->logString = "Recv thread stopped";
				Logger::GetInstance().WriteLog(log);
				continue;
			}
			else
			{
				auto log = Logger::MakeLogObject<ClientLog>();
				log->logString = "recvfrom() error with " + error;
				Logger::GetInstance().WriteLog(log);
				continue;
			}
		}

		if (not buffer->Decode() || buffer->GetUseSize() != GetPayloadLength(*buffer))
		{
			continue;
		}

		ProcessRecvPacket(*buffer);
	}
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
			break;
		}
		break;
		default:
		{
			auto log = Logger::MakeLogObject<ClientLog>();
			log->logString = "Invalid send thread wait result. Error is " + WSAGetLastError();
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
	tickSet.beforeTick = tickSet.nowTick;

	std::list<std::pair<PacketSequence, SendPacketInfo*>> copyList;

	while (threadStopFlag == true)
	{
		{
			std::scoped_lock lock(sendPacketInfoMapLock);
			copyList.assign(sendPacketInfoMap.begin(), sendPacketInfoMap.end());
		}

		for (auto& sendedPacketInfo : copyList)
		{
			if (sendedPacketInfo.second->sendTimeStamp < tickSet.nowTick)
			{
				continue;
			}

			if (++sendedPacketInfo.second->retransmissionCount >= maxPacketRetransmissionCount)
			{
				auto log = Logger::MakeLogObject<ClientLog>();
				log->logString = "The maximum number of packet retransmission controls has been exceeded, and RUDPClientCore terminates";
				Logger::GetInstance().WriteLog(log);

				isConnected = false;
				break;
			}

			SendPacket(*sendedPacketInfo.second->GetBuffer(), sendedPacketInfo.second->sendPacektSequence);
		}

		SleepRemainingFrameTime(tickSet, retransmissionThreadSleepMs);
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
	{
		NetBuffer::AddRefCount(&receivedBuffer);

		std::scoped_lock lock(recvPacketHoldingQueueLock);
		recvPacketHoldingQueue.emplace(RecvPacketInfo{ &receivedBuffer, packetSequence });

		SendReplyToServer(packetSequence);
	}
		break;
	case PACKET_TYPE::SendReplyType:
	{
		OnSendReply(receivedBuffer);
	}
		break;
	default:
	{
		auto log = Logger::MakeLogObject<ClientLog>();
		log->logString = "Invalid packet type " + static_cast<unsigned char>(packetType);
		Logger::GetInstance().WriteLog(log);
	}
		break;
	}
}

void RUDPClientCore::OnSendReply(NetBuffer& recvPacket)
{
	PacketSequence packetSequence;
	recvPacket >> packetSequence;

	if (lastSendPacketSequence < packetSequence)
	{
		return;
	}

	if (packetSequence == 0)
	{
		isConnected = true;
	}

	SendPacketInfo* sendedPacketInfo = nullptr;
	{
		std::scoped_lock lock(sendPacketInfoMapLock);
		auto itor = sendPacketInfoMap.find(packetSequence);
		if (itor != sendPacketInfoMap.end())
		{
			sendedPacketInfo = itor->second;
		}

		sendPacketInfoMap.erase(packetSequence);
	}
}

void RUDPClientCore::SendReplyToServer(const PacketSequence recvPacketSequence)
{
	auto& buffer = *NetBuffer::Alloc();

	PACKET_TYPE packetType = PACKET_TYPE::SendReplyType;
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
		if (sendto(rudpSocket, packet->GetBufferPtr(), packet->GetUseSize(), 0, (const sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
		{
			auto log = Logger::MakeLogObject<ClientLog>();
			log->logString = "sendto() failed with error code " + WSAGetLastError();
			Logger::GetInstance().WriteLog(log);
			continue;
		}
	}
}

void RUDPClientCore::SleepRemainingFrameTime(OUT TickSet& tickSet, unsigned int intervalMs)
{
	tickSet.nowTick = GetTickCount64();
	UINT64 deltaTick = tickSet.nowTick - tickSet.beforeTick;

	if (deltaTick < intervalMs && deltaTick > 0)
	{
		Sleep(intervalMs - static_cast<DWORD>(deltaTick));
	}

	tickSet.beforeTick = tickSet.nowTick;
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

		if (holdingPacketInfo.packetSequence < recvPacketSequence)
		{
			recvPacketHoldingQueue.pop();
			continue;
		}
		else if (holdingPacketInfo.packetSequence != recvPacketSequence)
		{
			return nullptr;
		}

		++recvPacketSequence;
		recvPacketHoldingQueue.pop();
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

WORD RUDPClientCore::GetPayloadLength(OUT NetBuffer& buffer)
{
	BYTE code;
	WORD payloadLength;
	buffer >> code >> payloadLength;

	if (code != NetBuffer::m_byHeaderCode)
	{
		auto log = Logger::MakeLogObject<ClientLog>();
		log->logString = "code : " + code;
		Logger::GetInstance().WriteLog(log);
		return 0;
	}

	return payloadLength;
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