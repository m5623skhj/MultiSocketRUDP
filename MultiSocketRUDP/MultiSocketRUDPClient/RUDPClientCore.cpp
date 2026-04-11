#include "PreCompile.h"
#include "RUDPClientCore.h"
#include "Logger.h"
#include "LogExtension.h"
#include "PacketManager.h"
#include "../Common/PacketCrypto/PacketCryptoHelper.h"
#include <mutex>

void SendPacketInfo::Free(SendPacketInfo* target)
{
	if (target == nullptr)
	{
		return;
	}

	if (target->refCount.fetch_sub(1, std::memory_order_release) == 1)
	{
		NetBuffer::Free(target->buffer);
		sendPacketInfoPool->Free(target);
	}
}

RUDPClientCore::RUDPClientCore()
	: serverAliveChecker([this] { Stop(); }
	                     , [this] { return GetNextRecvPacketSequence(); })
{
}

bool RUDPClientCore::Start(const std::wstring& clientCoreOptionFile, const std::wstring& sessionGetterOptionFilePath, const bool printLogToConsole)
{
	threadStopFlag = false;
	Logger::GetInstance().RunLoggerThread(printLogToConsole);

	if (not ReadOptionFile(clientCoreOptionFile, sessionGetterOptionFilePath))
	{
		return false;
	}

	{
		std::scoped_lock lock(clientCountInThisProcessLock);
		if (clientCountInThisProcess == 0)
		{
			WSADATA wsaData;
			if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
			{
				LOG_ERROR(std::format("WSAStartup failed GetSessionFromServer() with error code {}", WSAGetLastError()));
				WSACleanup();
				return false;
			}
		}

		++clientCountInThisProcess;
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
	if (sessionBrokerSocket != INVALID_SOCKET)
	{
		closesocket(sessionBrokerSocket);
		sessionBrokerSocket = INVALID_SOCKET;
	}

	SetEvent(sendEventHandles[1]);
	threadStopFlag = true;

	JoinThreads();

	if (rudpSocket != INVALID_SOCKET)
	{
		closesocket(rudpSocket);
		rudpSocket = INVALID_SOCKET;
	}

	if (sendEventHandles[0] != nullptr)
	{
		CloseHandle(sendEventHandles[0]);
		sendEventHandles[0] = nullptr;
	}
	if (sendEventHandles[1] != nullptr)
	{
		CloseHandle(sendEventHandles[1]);
		sendEventHandles[1] = nullptr;
	}

	{
		std::scoped_lock lock(sendPacketInfoMapLock);
		for (auto info : sendPacketInfoMap | std::views::values)
		{
			SendPacketInfo::Free(info);
		}
		sendPacketInfoMap.clear();
	}

	{
		std::scoped_lock lock(pendingPacketQueueLock);
		while (not pendingPacketQueue.empty())
		{
			auto [sequence, buffer] = pendingPacketQueue.top();
			pendingPacketQueue.pop();
			NetBuffer::Free(buffer);
		}
	}
	
	{
		std::scoped_lock lock(clientCountInThisProcessLock);
		--clientCountInThisProcess;
		if (clientCountInThisProcess == 0)
		{
			WSACleanup();
		}
	}
	isStopped = true;

	if (sessionKeyHandle != nullptr)
	{
		CryptoHelper::DestroySymmetricKeyHandle(sessionKeyHandle);
		sessionKeyHandle = nullptr;
	}

	if (keyObjectBuffer != nullptr)
	{
		delete[] keyObjectBuffer;
		keyObjectBuffer = nullptr;
	}
}

void RUDPClientCore::JoinThreads()
{
	serverAliveChecker.StopServerAliveCheck();
	if (retransmissionThread.joinable())
	{
		retransmissionThread.join();
	}
	if (sendThread.joinable())
	{
		sendThread.join();
	}
	if (recvThread.joinable())
	{
		recvThread.join();
	}
	Logger::GetInstance().StopLoggerThread();
}

bool RUDPClientCore::CreateRUDPSocket()
{
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(port);
	InetPtonA(AF_INET, serverIp.c_str(), &serverAddr.sin_addr);

	if (rudpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); rudpSocket == INVALID_SOCKET)
	{
		LOG_ERROR(std::format("socket() failed with error {}", WSAGetLastError()));
		return false;
	}

	sockaddr_in recvAddr;
	recvAddr.sin_family = AF_INET;
	recvAddr.sin_addr.s_addr = INADDR_ANY;
	recvAddr.sin_port = 0;

	if (bind(rudpSocket, reinterpret_cast<sockaddr*>(&recvAddr), sizeof(recvAddr)) == SOCKET_ERROR)
	{
		LOG_ERROR(std::format("bind() failed with error {}", WSAGetLastError()));
		closesocket(rudpSocket);
		return false;
	}

	return true;
}

void RUDPClientCore::SendConnectPacket()
{
	NetBuffer& connectPacket = *NetBuffer::Alloc();
	constexpr PacketSequence packetSequence = 0;
	constexpr auto packetType = PACKET_TYPE::CONNECT_TYPE;
	
	connectPacket << packetType << packetSequence << sessionId;
	SendPacket(connectPacket, packetSequence, true);
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
			
			const auto log = Logger::MakeLogObject<ClientLog>();
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
			const auto log = Logger::MakeLogObject<ClientLog>();
			log->logString = "Send thread stopped";
			Logger::GetInstance().WriteLog(log);
			return;
		}
		default:
		{
			LOG_ERROR(std::format("RunSendThread() : Invalid send thread wait result. Error is {}", WSAGetLastError()));
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

	std::list<SendPacketInfo*> retransmitTargets;
	while (not threadStopFlag)
	{
		{
			std::scoped_lock lock(sendPacketInfoMapLock);
			retransmitTargets.clear();
			for (auto& [sequence, info] : sendPacketInfoMap)
			{
				if (info->retransmissionTimeStamp >= tickSet.nowTick)
				{
					continue;
				}

				info->AddRefCount();
				retransmitTargets.push_back(info);
			}
		}

		for (auto* sendPacketInfo : retransmitTargets)
		{
			if (threadStopFlag)
			{
				SendPacketInfo::Free(sendPacketInfo);
				continue;
			}

			bool shouldSkip = false;
			bool shouldDisconnect = false;
			{
				std::scoped_lock lock(sendPacketInfoMapLock);
				if (sendPacketInfoMap.find(sendPacketInfo->sendPacketSequence) == sendPacketInfoMap.end())
				{
					shouldSkip = true;
				}
				else if (++sendPacketInfo->retransmissionCount >= maxPacketRetransmissionCount)
				{
					shouldDisconnect = true;
				}
			}

			if (shouldSkip)
			{
				SendPacketInfo::Free(sendPacketInfo);
				continue;
			}

			if (shouldDisconnect)
			{
				LOG_ERROR("The maximum number of packet retransmission controls has been exceeded, and RUDPClientCore terminates");
				isConnected = false;
				threadStopFlag = true;
				SendPacketInfo::Free(sendPacketInfo);
				continue;
			}

			sendPacketInfo->retransmissionTimeStamp = GetTickCount64() + retransmissionThreadSleepMs;
			SendPacket(*sendPacketInfo);
			SendPacketInfo::Free(sendPacketInfo);
		}

		SleepRemainingFrameTime(tickSet, retransmissionThreadSleepMs);
	}

	const auto log = Logger::MakeLogObject<ClientLog>();
	log->logString = "Retransmission thread stopped";
	Logger::GetInstance().WriteLog(log);
}

void RUDPClientCore::OnRecvStream(NetBuffer& recvBuffer, int recvSize)
{
	while (recvSize > df_HEADER_SIZE)
	{
		NetBuffer* recvPacketBuffer = NetBuffer::Alloc();
		recvBuffer.ReadBuffer(recvPacketBuffer->GetBufferPtr(), df_HEADER_SIZE);
		recvPacketBuffer->m_iRead = 0;

		const WORD payloadLength = GetPayloadLength(*recvPacketBuffer);
		if (payloadLength <= 0 || payloadLength > dfDEFAULTSIZE || payloadLength > recvSize)
		{
			NetBuffer::Free(recvPacketBuffer);
			break;
		}
		const int packetSize = (payloadLength + df_HEADER_SIZE);
		recvSize -= packetSize;
		
		recvBuffer.ReadBuffer(recvPacketBuffer->GetWriteBufferPtr(), payloadLength);
		recvPacketBuffer->m_iRead = df_HEADER_SIZE;
		recvPacketBuffer->m_iWrite = static_cast<WORD>(packetSize);

		ProcessRecvPacket(*recvPacketBuffer);
		NetBuffer::Free(recvPacketBuffer);
	}
}

void RUDPClientCore::ProcessRecvPacket(OUT NetBuffer& receivedBuffer)
{
	PACKET_TYPE packetType;
	PacketSequence packetSequence;
	receivedBuffer >> packetType;

	bool isCorePacket = true;
	auto direction = PACKET_DIRECTION::SERVER_TO_CLIENT;

	switch (packetType)
	{
	case PACKET_TYPE::SEND_TYPE:
	{
		isCorePacket = false;
		[[fallthrough]];
	}
	case PACKET_TYPE::HEARTBEAT_TYPE:
	{
		if (not PacketCryptoHelper::DecodePacket(
			receivedBuffer,
			sessionSalt,
			SESSION_SALT_SIZE,
			sessionKeyHandle,
			isCorePacket,
			direction
		))
		{
			return;
		}

		receivedBuffer >> packetSequence;
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
		direction = PACKET_DIRECTION::SERVER_TO_CLIENT_REPLY;

		if (not PacketCryptoHelper::DecodePacket(
			receivedBuffer,
			sessionSalt,
			SESSION_SALT_SIZE,
			sessionKeyHandle,
			isCorePacket,
			direction
		))
		{
			return;
		}

		receivedBuffer >> packetSequence;
		OnSendReply(receivedBuffer, packetSequence);
		break;
	}
	default:
	{
		LOG_ERROR(std::format("Invalid packet type received: {}", static_cast<unsigned char>(packetType)));
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

	BYTE remoteWindow;
	recvPacket >> remoteWindow;
	remoteAdvertisedWindow.store(remoteWindow, std::memory_order_relaxed);
	lastAckedSequence.store(packetSequence, std::memory_order_relaxed);

	if (packetSequence == 0 && not isConnected)
	{
		isConnected = true;
		serverAliveChecker.StartServerAliveCheck(serverAliveCheckMs);
	}

	{
		std::scoped_lock lock(sendPacketInfoMapLock);
		const auto itor = sendPacketInfoMap.find(packetSequence);
		if (itor != sendPacketInfoMap.end())
		{
			SendPacketInfo* info = itor->second;
			sendPacketInfoMap.erase(itor);
			SendPacketInfo::Free(info);
		}
	}

	TryFlushPendingQueue();
}

void RUDPClientCore::SendReplyToServer(const PacketSequence inRecvPacketSequence, const PACKET_TYPE packetType)
{
	auto& buffer = *NetBuffer::Alloc();

	buffer << packetType << inRecvPacketSequence;
	PacketCryptoHelper::EncodePacket(
		buffer,
		inRecvPacketSequence,
		PACKET_DIRECTION::CLIENT_TO_SERVER_REPLY,
		sessionSalt,
		SESSION_SALT_SIZE,
		sessionKeyHandle,
		true
	);

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
			LOG_ERROR("sendBufferQueue.Dequeue() failed");
			continue;
		}

		if (rudpSocket == INVALID_SOCKET)
		{
			NetBuffer::Free(packet);
			return;
		}

		if (sendto(rudpSocket, packet->GetBufferPtr(), packet->GetAllUseSize(), 0, reinterpret_cast<const sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR)
		{
			LOG_ERROR(std::format("sendto() failed with error code {}", WSAGetLastError()));
		}

		NetBuffer::Free(packet);
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
		const auto holdingPacketInfo = recvPacketHoldingQueue.top();

		if (holdingPacketInfo.packetSequence < nextRecvPacketSequence)
		{
			recvPacketHoldingQueue.pop();
			NetBuffer::Free(holdingPacketInfo.buffer);
			continue;
		}

		if (holdingPacketInfo.packetSequence != nextRecvPacketSequence)
		{
			return nullptr;
		}

		++nextRecvPacketSequence;
		recvPacketHoldingQueue.pop();
		if (holdingPacketInfo.packetType == PACKET_TYPE::HEARTBEAT_TYPE)
		{
			NetBuffer::Free(holdingPacketInfo.buffer);
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
		LOG_ERROR("Buffer is nullptr in RUDPSession::SendPacket()");
		return;
	}

	constexpr auto packetType = PACKET_TYPE::SEND_TYPE;
	const PacketSequence packetSequence = ++lastSendPacketSequence;
	*buffer << packetType << packetSequence << packet.GetPacketId();
	packet.PacketToBuffer(*buffer);

	return SendPacket(*buffer, packetSequence, false);
}

void RUDPClientCore::Disconnect()
{
	NetBuffer* buffer = NetBuffer::Alloc();
	if (buffer == nullptr)
	{
		LOG_ERROR("Buffer is nullptr in RUDPSession::Disconnect()");
		return;
	}

	constexpr auto packetType = PACKET_TYPE::DISCONNECT_TYPE;
	constexpr PacketSequence packetSequence = 0;
	*buffer << packetType;

	PacketCryptoHelper::EncodePacket(
		*buffer,
		packetSequence,
		PACKET_DIRECTION::CLIENT_TO_SERVER,
		sessionSalt,
		SESSION_SALT_SIZE,
		sessionKeyHandle,
		true
	);

	{
		std::scoped_lock lock(sendBufferQueueLock);
		sendBufferQueue.Enqueue(buffer);
	}

	ReleaseSemaphore(sendEventHandles[0], 1, nullptr);
	isConnected = false;
}

#if _DEBUG
void RUDPClientCore::SendPacketForTest(char* streamData, const int streamSize)
{
	NetBuffer* buffer = NetBuffer::Alloc();
	if (buffer == nullptr)
	{
		LOG_ERROR("Buffer is nullptr in RUDPSession::SendPacketForTest()");
		return;
	}

	constexpr auto packetType = PACKET_TYPE::SEND_TYPE;
	const PacketSequence packetSequence = ++lastSendPacketSequence;
	*buffer << packetType << packetSequence;
	buffer->WriteBuffer(streamData, streamSize);
	SendPacket(*buffer, packetSequence, true);
}
#endif

void RUDPClientCore::SendPacket(OUT NetBuffer& buffer, const PacketSequence inSendPacketSequence, const bool isCorePacket)
{
	PacketCryptoHelper::EncodePacket(
		buffer,
		inSendPacketSequence,
		PACKET_DIRECTION::CLIENT_TO_SERVER,
		sessionSalt,
		SESSION_SALT_SIZE,
		sessionKeyHandle,
		isCorePacket
	);

	if (not isCorePacket)
	{
		const BYTE window = remoteAdvertisedWindow.load(std::memory_order_relaxed);
		if (window == 0)
		{
			std::scoped_lock lock(pendingPacketQueueLock);
			pendingPacketQueue.push({ inSendPacketSequence, &buffer });
			return;
		}


		BYTE outstanding;
		{
			std::scoped_lock lock(sendPacketInfoMapLock);
			outstanding = static_cast<BYTE>(sendPacketInfoMap.size());
		}

		if (outstanding >= window)
		{
			std::scoped_lock lock(pendingPacketQueueLock);
			pendingPacketQueue.push({ inSendPacketSequence, &buffer });
			return;
		}
	}

	RegisterSendPacketInfo(buffer, inSendPacketSequence);
}

void RUDPClientCore::SendPacket(const SendPacketInfo& sendPacketInfo)
{
	NetBuffer::AddRefCount(sendPacketInfo.buffer);
	{
		std::scoped_lock lock(sendBufferQueueLock);
		sendBufferQueue.Enqueue(sendPacketInfo.buffer);
	}
	ReleaseSemaphore(sendEventHandles[0], 1, nullptr);
}

void RUDPClientCore::RegisterSendPacketInfo(NetBuffer& buffer, const PacketSequence inSendPacketSequence)
{
	auto sendPacketInfo = sendPacketInfoPool->Alloc();
	if (sendPacketInfo == nullptr)
	{
		LOG_ERROR("SendPacketInfo is nullptr in RegisterSendPacketInfo()");
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

	ReleaseSemaphore(sendEventHandles[0], 1, nullptr);
}

void RUDPClientCore::TryFlushPendingQueue()
{
	const BYTE window = remoteAdvertisedWindow.load(std::memory_order_relaxed);
	if (window == 0)
	{
		return;
	}

	std::scoped_lock pendingLock(pendingPacketQueueLock);
	while (not pendingPacketQueue.empty())
	{
		{
			std::scoped_lock infoLock(sendPacketInfoMapLock);
			if (const auto outstanding = static_cast<BYTE>(sendPacketInfoMap.size()); outstanding >= window)
			{
				break;
			}
		}

		auto [sequence, buffer] = pendingPacketQueue.top();
		pendingPacketQueue.pop();

		RegisterSendPacketInfo(*buffer, sequence);
	}
}

WORD RUDPClientCore::GetPayloadLength(const NetBuffer& buffer)
{
	constexpr int payloadLengthPosition = 1;

	return *reinterpret_cast<WORD*>(&buffer.m_pSerializeBuffer[buffer.m_iRead + payloadLengthPosition]);
}

bool RUDPClientCore::ReadOptionFile(const std::wstring& clientCoreOptionFile, const std::wstring& sessionGetterOptionFilePath)
{
	if (not ReadClientCoreOptionFile(clientCoreOptionFile))
	{
		LOG_ERROR("RUDPClientCore::ReadClientCoreOptionFile() failed");
		return false;
	}

	if (not ReadSessionGetterOptionFile(sessionGetterOptionFilePath))
	{
		LOG_ERROR("RUDPClientCore::ReadSessionGetterOptionFile() failed");
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

	const int iJumpBom = ftell(fp);
	fseek(fp, 0, SEEK_END);
	const int iFileSize = ftell(fp);
	fseek(fp, iJumpBom, SEEK_SET);
	const int fileSize = static_cast<int>(fread_s(cBuffer, BUFFER_MAX, sizeof(WCHAR), iFileSize / 2, fp));
	const int amend = iFileSize - fileSize;
	fclose(fp);

	cBuffer[iFileSize - amend] = '\0';
	WCHAR* pBuff = cBuffer;

	if (!parser.GetValue_Short(pBuff, L"CORE", L"MAX_PACKET_RETRANSMISSION_COUNT", reinterpret_cast<short*>(&maxPacketRetransmissionCount)))
	{
		return false;
	}
	if (!parser.GetValue_Int(pBuff, L"CORE", L"RETRANSMISSION_MS", reinterpret_cast<int*>(&retransmissionThreadSleepMs)))
	{
		return false;
	}
	if (!parser.GetValue_Int(pBuff, L"CORE", L"SERVER_ALIVE_CHECK_MS", reinterpret_cast<int*>(&serverAliveCheckMs)))
	{
		return false;
	}

	return true;
}
