#include "PreCompile.h"
#include "MultiSocketRUDPCore.h"
#include "BuildConfig.h"
#include "EssentialHandler.h"
#include "LogExtension.h"
#include "Logger.h"
#include "Ticker.h"

void IOContext::InitContext(const SessionIdType inOwnerSessionId, const RIO_OPERATION_TYPE inIOType)
{
	ownerSessionId = inOwnerSessionId;
	ioType = inIOType;
}

MultiSocketRUDPCore::MultiSocketRUDPCore()
	: contextPool(2, false)
{
}

bool MultiSocketRUDPCore::StartServer(const std::wstring& coreOptionFilePath, const std::wstring& sessionBrokerOptionFilePath, bool printLogToConsole)
{
	Logger::GetInstance().RunLoggerThread(printLogToConsole);

	if (not ReadOptionFile(coreOptionFilePath, sessionBrokerOptionFilePath))
	{
		const auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = "Option file read failed";
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	if (not EssentialHandlerManager::GetInst().IsRegisteredAllEssentialHandler())
	{
		const auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = "Required handler not registered";
		Logger::GetInstance().WriteLog(log);
		EssentialHandlerManager::GetInst().PrintUnregisteredEssentialHandler();
		return false;
	}

	if (not InitNetwork())
	{
		StopServer();
		const auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = "InitNetwork failed";
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	if (not InitRIO())
	{
		StopServer();
		const auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = "InitRIO failed";
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	if (not RunAllThreads())
	{
		StopServer();
		const auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = "RunAllThreads() failed";
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	return true;
}

void MultiSocketRUDPCore::StopServer()
{
	threadStopFlag = true;

#if USE_IOCP_SESSION_BROKER
	sessionBroker.Stop();
#else
	closesocket(sessionBrokerListenSocket);
	sessionBrokerThread.join();
#endif
	CloseAllSessions();
	delete[] rioCQList;

	SetEvent(logicThreadEventStopHandle);
	for (ThreadIdType i = 0; i < numOfWorkerThread; ++i)
	{
		ioWorkerThreads[i].join();
		recvLogicWorkerThreads[i].join();
		retransmissionThreads[i].join();
		CloseHandle(recvLogicThreadEventHandles[i]);
	}

	SetEvent(sessionReleaseEventHandle);
	sessionReleaseThread.join();
	heartbeatThread.join();
	Ticker::GetInstance().Stop();

	CloseHandle(logicThreadEventStopHandle);
	CloseHandle(sessionReleaseEventHandle);

	ClearAllSession();

	Logger::GetInstance().StopLoggerThread();

	isServerStopped = true;
	const auto log = Logger::MakeLogObject<ServerLog>();
	log->logString = "Server stop";
	Logger::GetInstance().WriteLog(log);
}

bool MultiSocketRUDPCore::SendPacket(SendPacketInfo* sendPacketInfo)
{
	if (const auto buffer = sendPacketInfo->GetBuffer(); buffer->m_bIsEncoded == false)
	{
		buffer->m_iWriteLast = buffer->m_iWrite;
		buffer->m_iWrite = 0;
		buffer->m_iRead = 0;
		buffer->Encode();
	}

	sendPacketInfo->owner->sendBuffer.sendPacketInfoQueue.Enqueue(sendPacketInfo);

	if (not DoSend(*sendPacketInfo->owner, sendPacketInfo->owner->threadId))
	{
		sendPacketInfoPool->Free(sendPacketInfo);
		return false;
	}

	return true;
}

void MultiSocketRUDPCore::DisconnectSession(const SessionIdType disconnectTargetSessionId)
{
	if (disconnectTargetSessionId >= sessionArray.size() || not sessionArray[disconnectTargetSessionId]->isUsingSession)
	{
		return;
	}

	{
		std::scoped_lock lock(unusedSessionIdListLock);
		unusedSessionIdList.emplace_back(disconnectTargetSessionId);
		sessionArray[disconnectTargetSessionId]->isUsingSession = false;
	}
	--connectedUserCount;

	const auto log = Logger::MakeLogObject<ServerLog>();
	log->logString = std::format("Session id {} is disconnected", disconnectTargetSessionId);
	Logger::GetInstance().WriteLog(log);
}

void MultiSocketRUDPCore::EraseSendPacketInfo(OUT SendPacketInfo* eraseTarget, const ThreadIdType threadId)
{
	if (eraseTarget == nullptr || eraseTarget->isErasedPacketInfo == true)
	{
		return;
	}

	do
	{
		if (eraseTarget->owner == nullptr)
		{
			break;
		}

		{
			std::scoped_lock lock(*sendPacketInfoListLock[threadId]);
			sendPacketInfoList[threadId].erase(eraseTarget->listItor);
			eraseTarget->isErasedPacketInfo = true;
		}
	} while (false);

	sendPacketInfoPool->Free(eraseTarget);
}

void MultiSocketRUDPCore::PushToDisconnectTargetSession(RUDPSession& session)
{
	std::scoped_lock lock(releaseSessionIdListLock);
	session.nowInReleaseThread = true;
	releaseSessionIdList.emplace_back(session.GetSessionId());
}

bool MultiSocketRUDPCore::InitNetwork()
{
	WSADATA wsaData;
	if (int result = WSAStartup(MAKEWORD(2, 2), &wsaData); result != 0)
	{
		const auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = std::format("WSAStartup failed {}", result);
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	sessionArray.reserve(numOfSockets);
	for (auto socketNumber = 0; socketNumber < numOfSockets; ++socketNumber)
	{
		sessionArray.emplace_back(new RUDPSession(*this));
		sessionArray[socketNumber]->sessionId = static_cast<SessionIdType>(socketNumber);
	}

	return true;
}

bool MultiSocketRUDPCore::InitRIO()
{
	GUID guid = WSAID_MULTIPLE_RIO;
	DWORD bytes = 0;

	const SOCKET tempSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	bool result = true;
	do
	{
		// For the purpose of obtaining the function table, any of the created sessions selected
		if (WSAIoctl(tempSocket, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER, &guid, sizeof(GUID)
			, &rioFunctionTable, sizeof(rioFunctionTable), &bytes, nullptr, nullptr))
		{
			const auto log = Logger::MakeLogObject<ServerLog>();
			log->logString = std::format("WSAIoctl_SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER with {}", WSAGetLastError());
			Logger::GetInstance().WriteLog(log);
			result = false;
			break;
		}

		rioCQList = new RIO_CQ[numOfWorkerThread];
		if (rioCQList == nullptr)
		{
			result = false;
			break;
		}
		for (int rioCQIndex = 0; rioCQIndex < numOfWorkerThread; ++rioCQIndex)
		{
			rioCQList[rioCQIndex] = rioFunctionTable.RIOCreateCompletionQueue(numOfSockets / numOfWorkerThread * MAX_SEND_BUFFER_SIZE, nullptr);
		}

		for (const auto& session : sessionArray)
		{
			session->threadId = session->sessionId % numOfWorkerThread;
			unusedSessionIdList.emplace_back(session->sessionId);
		}
	} while (false);
	
	if (tempSocket != INVALID_SOCKET && result == false)
	{
		closesocket(tempSocket);
	}

	return result;
}

RIO_BUFFERID MultiSocketRUDPCore::RegisterRIOBuffer(char* targetBuffer, const unsigned int targetBufferSize) const
{
	if (targetBuffer == nullptr)
	{
		return RIO_INVALID_BUFFERID;
	}

	const RIO_BUFFERID bufferId = rioFunctionTable.RIORegisterBuffer(targetBuffer, targetBufferSize);
	if (bufferId == RIO_INVALID_BUFFERID)
	{
		const auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = std::format("RIORegisterBuffer failed with error code {}", WSAGetLastError());
		Logger::GetInstance().WriteLog(log);
	}

	return bufferId;
}

bool MultiSocketRUDPCore::RunAllThreads()
{
	sendPacketInfoList.reserve(numOfWorkerThread);
	sendPacketInfoListLock.reserve(numOfWorkerThread);
	ioWorkerThreads.reserve(numOfWorkerThread);
	recvLogicWorkerThreads.reserve(numOfWorkerThread);
	retransmissionThreads.reserve(numOfWorkerThread);

	logicThreadEventStopHandle = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	sessionReleaseEventHandle = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	recvLogicThreadEventHandles.reserve(numOfWorkerThread);
	ioCompletedContexts.reserve(numOfWorkerThread);

	Ticker::GetInstance().Start(timerTickMs);
	sessionReleaseThread = std::jthread([this]() { RunSessionReleaseThread(); });
	heartbeatThread = std::jthread([this]() { RunHeartbeatThread(); });
	for (unsigned char id = 0; id < numOfWorkerThread; ++id)
	{
		ioCompletedContexts.emplace_back();

		recvLogicThreadEventHandles.emplace_back(CreateSemaphore(nullptr, 0, LONG_MAX, nullptr));
		sendPacketInfoList.emplace_back();
		sendPacketInfoListLock.push_back(std::make_unique<std::mutex>());

		ioWorkerThreads.emplace_back([this, id]() { this->RunIOWorkerThread(static_cast<ThreadIdType>(id)); });
		recvLogicWorkerThreads.emplace_back([this, id]() { this->RunRecvLogicWorkerThread(static_cast<ThreadIdType>(id)); });
		retransmissionThreads.emplace_back([this, id]() { this->RunRetransmissionThread(static_cast<ThreadIdType>(id)); });
	}

	Sleep(1000);
	if (not RunSessionBroker())
	{
		const auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = "RunSessionBroker() failed";
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	return true;
}

bool MultiSocketRUDPCore::RunSessionBroker()
{
#if USE_IOCP_SESSION_BROKER
	if (not sessionBroker.Start(sessionBrokerOptionFilePath))
	{
		CloseAllSockets();
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = "SessionBroker start falied";
		Logger::GetInstance().WriteLog(log);
		return false;
	}
#else
	sessionBrokerThread = std::jthread([this]() { this->RunSessionBrokerThread(sessionBrokerPort, coreServerIp); });
#endif

	return true;
}

SOCKET MultiSocketRUDPCore::CreateRUDPSocket()
{
	SOCKET sock = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_REGISTERED_IO);
	if (sock == INVALID_SOCKET)
	{
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = std::format("Socket create failed {}", WSAGetLastError());
		Logger::GetInstance().WriteLog(log);
		return INVALID_SOCKET;
	}

	sockaddr_in serverAddr = {};

	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.S_un.S_addr = INADDR_ANY;
	serverAddr.sin_port = 0;

	if (bind(sock, reinterpret_cast<SOCKADDR*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR)
	{
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = std::format("Bind failed {}", WSAGetLastError());
		Logger::GetInstance().WriteLog(log);
		closesocket(sock);
		return INVALID_SOCKET;
	}

	socklen_t len = sizeof(serverAddr);
	getsockname(sock, reinterpret_cast<sockaddr*>(&serverAddr), &len);

	return sock;
}

void MultiSocketRUDPCore::CloseAllSessions()
{
	for (const auto& session : sessionArray)
	{
		if (session->sock != INVALID_SOCKET)
		{
			closesocket(session->sock);
			continue;
		}
	}

	connectedUserCount = 0;
}

void MultiSocketRUDPCore::ClearAllSession()
{
	{
		std::scoped_lock lock(unusedSessionIdListLock);
		unusedSessionIdList.clear();
	}

	{
		std::scoped_lock lock(releaseSessionIdListLock);
		releaseSessionIdList.clear();
	}

	{
		for (const auto& session : sessionArray)
		{
			delete session;
		}

		sessionArray.clear();
	}
}

RUDPSession* MultiSocketRUDPCore::AcquireSession()
{
	RUDPSession* session;
	{
		std::scoped_lock lock(unusedSessionIdListLock);

		if (unusedSessionIdList.empty() == true)
		{
			return nullptr;
		}

		const SessionIdType sessionId = unusedSessionIdList.front();
		unusedSessionIdList.pop_front();

		session = sessionArray[sessionId];
		if (session->isUsingSession == true)
		{
			unusedSessionIdList.push_back(sessionId);
			return nullptr;
		}
		session->isUsingSession = true;
	}

	return session;
}

RUDPSession* MultiSocketRUDPCore::GetUsingSession(const SessionIdType sessionId) const
{
	if (sessionArray.size() <= sessionId || not sessionArray[sessionId]->isUsingSession)
	{
		return nullptr;
	}

	return sessionArray[sessionId];
}

void MultiSocketRUDPCore::RunIOWorkerThread(const ThreadIdType threadId)
{
	RIORESULT rioResults[MAX_RIO_RESULT];

	TickSet tickSet;
	tickSet.nowTick = GetTickCount64();

	while (not threadStopFlag)
	{
		ZeroMemory(rioResults, sizeof(rioResults));

		const ULONG numOfResults = rioFunctionTable.RIODequeueCompletion(rioCQList[threadId], rioResults, MAX_RIO_RESULT);
		for (ULONG i = 0; i < numOfResults; ++i)
		{
			const auto context = GetIOCompletedContext(rioResults[i]);
			if (context == nullptr)
			{
				continue;
			}

			if (not IOCompleted(context, rioResults[i].BytesTransferred, threadId))
			{
				contextPool.Free(context);
				// error handling
				continue;
			}
		}

#if USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME
		SleepRemainingFrameTime(tickSet, workerThreadOneFrameMs);
#endif
	}

	const auto log = Logger::MakeLogObject<ServerLog>();
	log->logString = "Worker thread stopped";
	Logger::GetInstance().WriteLog(log);
}

void MultiSocketRUDPCore::RunRecvLogicWorkerThread(const ThreadIdType threadId)
{
	const HANDLE eventHandles[2] = { recvLogicThreadEventHandles[threadId], logicThreadEventStopHandle };
	while (not threadStopFlag)
	{
		switch (WaitForMultipleObjects(2, eventHandles, FALSE, INFINITE))
		{
		case WAIT_OBJECT_0:
		{
			OnRecvPacket(threadId);
		}
		break;
		case WAIT_OBJECT_0 + 1:
		{
			Sleep(LOGIC_THREAD_STOP_SLEEP_TIME);
			OnRecvPacket(threadId);
			auto log = Logger::MakeLogObject<ServerLog>();
			log->logString = std::format("Logic thread stop. ThreadId is {}", threadId);
			Logger::GetInstance().WriteLog(log);
			break;
		}
		default:
		{
			auto log = Logger::MakeLogObject<ServerLog>();
			log->logString = std::format("Invalid logic thread wait result. Error is {}", WSAGetLastError());
			Logger::GetInstance().WriteLog(log);
			break;
		}
		}
	}
}

void MultiSocketRUDPCore::RunRetransmissionThread(const ThreadIdType threadId)
{
	TickSet tickSet;
	tickSet.nowTick = GetTickCount64();

	auto& thisThreadSendPacketInfoList = sendPacketInfoList[threadId];
	auto& thisThreadSendPacketInfoListLock = *sendPacketInfoListLock[threadId];

	std::list<SendPacketInfo*> copyList;
	unsigned short numOfTimeoutSession{};

	while (not threadStopFlag)
	{
		{
			std::scoped_lock lock(thisThreadSendPacketInfoListLock);
			copyList.assign(thisThreadSendPacketInfoList.begin(), thisThreadSendPacketInfoList.end());
		}
		
		for (const auto& sendPacketInfo : copyList)
		{
			if (sendPacketInfo->retransmissionTimeStamp > tickSet.nowTick || sendPacketInfo->owner->nowInReleaseThread || sendPacketInfo->isErasedPacketInfo)
			{
				continue;
			}

			if (++sendPacketInfo->retransmissionCount >= maxPacketRetransmissionCount)
			{
				PushToDisconnectTargetSession(*sendPacketInfo->owner);
				++numOfTimeoutSession;
				continue;
			}

			SendPacket(sendPacketInfo);
		}

		if (numOfTimeoutSession > 0)
		{
			SetEvent(sessionReleaseEventHandle);
			numOfTimeoutSession = {};
		}

		SleepRemainingFrameTime(tickSet, retransmissionThreadSleepMs);
	}
}

void MultiSocketRUDPCore::RunSessionReleaseThread()
{
	while (not threadStopFlag)
	{
		switch (WaitForSingleObject(sessionReleaseEventHandle, INFINITE))
		{
		case WAIT_OBJECT_0:
		{
			std::scoped_lock lock(releaseSessionIdListLock);
			for (const auto& releaseSessionId : releaseSessionIdList)
			{
				if (const auto releaseSession = GetUsingSession(releaseSessionId))
				{
					if (releaseSession->sendBuffer.ioMode == IO_MODE::IO_SENDING ||
						releaseSession->nowInProcessingRecvPacket)
					{
						continue;
					}

					releaseSession->Disconnect();
					releaseSession->InitializeSession();
				}
			}
			releaseSessionIdList.clear();
		}
		break;
		default:
		{
			auto log = Logger::MakeLogObject<ServerLog>();
			log->logString = std::format("Invalid release thread wait result. Error is {}", WSAGetLastError());
			Logger::GetInstance().WriteLog(log);
		}
		break;
		}
	}
}

void MultiSocketRUDPCore::RunHeartbeatThread() const
{
	TickSet tickSet;
	tickSet.nowTick = GetTickCount64();

	while (not threadStopFlag)
	{
		for (const auto& session : sessionArray)
		{
			if (session->isUsingSession == false || session->isConnected == false)
			{
				continue;
			}

			session->SendHeartbeatPacket();
		}

		SleepRemainingFrameTime(tickSet, heartbeatThreadSleepMs);
	}
}

void MultiSocketRUDPCore::SleepRemainingFrameTime(OUT TickSet& tickSet, const unsigned int intervalMs)
{
	UINT64 now = GetTickCount64();
	UINT64 delta = now - tickSet.nowTick;

	if (delta < intervalMs)
	{
		Sleep(static_cast<DWORD>(intervalMs - delta));
	}

	tickSet.nowTick = GetTickCount64();
}

IOContext* MultiSocketRUDPCore::GetIOCompletedContext(RIORESULT& rioResult)
{
	IOContext* context = reinterpret_cast<IOContext*>(rioResult.RequestContext);
	if (context == nullptr)
	{
		return nullptr;
	}

	if (rioResult.Status != 0)
	{
		const auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = std::format("RIO operation failed with error code {}", rioResult.Status);
		Logger::GetInstance().WriteLog(log);
		contextPool.Free(context);
		return nullptr;
	}

	context->session = GetUsingSession(context->ownerSessionId);
	if (context->session == nullptr)
	{
		return nullptr;
	}

	return context;
}

bool MultiSocketRUDPCore::IOCompleted(OUT IOContext* contextResult, const ULONG transferred, const BYTE threadId)
{
	if (contextResult == nullptr)
	{
		return false;
	}

	switch (contextResult->ioType)
	{
	case RIO_OPERATION_TYPE::OP_RECV:
	{
		if (RecvIOCompleted(contextResult, transferred, threadId))
		{
			// Release session?
		}
		return true;
	}
	case RIO_OPERATION_TYPE::OP_SEND:
	{
		return SendIOCompleted(*contextResult->session, threadId);
	}
	default:
	{
		const auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = std::format("Invalid rio operation type. Type is {}", static_cast<unsigned char>(contextResult->ioType));
		Logger::GetInstance().WriteLog(log);
	}
	break;
	}

	return false;
}

bool MultiSocketRUDPCore::RecvIOCompleted(OUT IOContext* contextResult, const ULONG transferred, const BYTE threadId)
{
	const auto buffer = NetBuffer::Alloc();
	if (memcpy_s(buffer->m_pSerializeBuffer, RECV_BUFFER_SIZE, contextResult->session->recvBuffer.buffer, transferred) != 0)
	{
		return false;
	}
	buffer->m_iWrite = static_cast<WORD>(transferred);

	contextResult->session->recvBuffer.recvBufferList.Enqueue(buffer);
	ioCompletedContexts[threadId].Enqueue(contextResult);
	ReleaseSemaphore(recvLogicThreadEventHandles[threadId], 1, nullptr);

	return DoRecv(*contextResult->session);
}

bool MultiSocketRUDPCore::SendIOCompleted(RUDPSession& session, const BYTE threadId)
{
	InterlockedExchange(reinterpret_cast<UINT*>(&session.sendBuffer.ioMode), static_cast<UINT>(IO_MODE::IO_NONE_SENDING));
	return DoSend(session, threadId);
}

void MultiSocketRUDPCore::OnRecvPacket(const BYTE threadId)
{
	while (ioCompletedContexts[threadId].GetRestSize() > 0)
	{
		IOContext* context = nullptr;
		if (ioCompletedContexts[threadId].Dequeue(&context) == false || context == nullptr)
		{
			continue;
		}

		NetBuffer* buffer = nullptr;
		do
		{
			if (context->session->recvBuffer.recvBufferList.Dequeue(&buffer) == false || buffer == nullptr)
			{
				break;
			}

			if (not buffer->Decode() || buffer->GetUseSize() != GetPayloadLength(*buffer))
			{
				break;
			}

			sockaddr_in clientAddr;
			std::ignore = memcpy_s(&clientAddr, sizeof(clientAddr), context->clientAddrBuffer, sizeof(clientAddr));
			if (not ProcessByPacketType(*context->session, clientAddr, *buffer))
			{
				NetBuffer::Free(buffer);
				return;
			}
		} while (false);

		if (buffer != nullptr)
		{
			NetBuffer::Free(buffer);
		}
	}
}

bool MultiSocketRUDPCore::DoRecv(const RUDPSession& session) const
{
	const auto context = session.recvBuffer.recvContext;
	if (context == nullptr)
	{
		const auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = "DoRecv() : context is nullptr";
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	if (rioFunctionTable.RIOReceiveEx(session.rioRQ, context.get(), 1, &context->localAddrRIOBuffer, &context->clientAddrRIOBuffer, nullptr, nullptr, 0, context.get()) == false)
	{
		const auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = std::format("RIOReceiveEx() failed with {}", WSAGetLastError());
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	return true;
}

bool MultiSocketRUDPCore::DoSend(OUT RUDPSession& session, const ThreadIdType threadId)
{
	while (true)
	{
		if (InterlockedCompareExchange(reinterpret_cast<UINT*>(&session.sendBuffer.ioMode), static_cast<UINT>(IO_MODE::IO_SENDING), static_cast<UINT>(IO_MODE::IO_NONE_SENDING)))
		{
			break;
		}

		if (session.sendBuffer.sendPacketInfoQueue.GetRestSize() == 0 &&
			session.sendBuffer.reservedSendPacketInfo == nullptr)
		{
			InterlockedExchange(reinterpret_cast<UINT*>(&session.sendBuffer.ioMode), static_cast<UINT>(IO_MODE::IO_NONE_SENDING));
			if (session.sendBuffer.sendPacketInfoQueue.GetRestSize() > 0)
			{
				continue;
			}
			break;
		}

		IOContext* sendContext = MakeSendContext(session, threadId);
		if (sendContext == nullptr)
		{
			return false;
		}

		return TryRIOSend(session, sendContext);
	}

	return true;
}

IOContext* MultiSocketRUDPCore::MakeSendContext(OUT RUDPSession& session, const ThreadIdType threadId)
{
	IOContext* context = contextPool.Alloc();
	context->InitContext(session.sessionId, RIO_OPERATION_TYPE::OP_SEND);
	context->BufferId = session.sendBuffer.sendBufferId;
	context->Offset = 0;
	context->Length = MakeSendStream(session, context, threadId);
	if (context->Length == 0)
	{
		contextPool.Free(context);
		return nullptr;
	}

	if (context->clientAddrRIOBuffer.BufferId == RIO_INVALID_BUFFERID)
	{
		if (context->clientAddrRIOBuffer.BufferId = RegisterRIOBuffer(context->clientAddrBuffer, sizeof(SOCKADDR_INET)); context->clientAddrRIOBuffer.BufferId == RIO_INVALID_BUFFERID)
		{
			const auto log = Logger::MakeLogObject<ServerLog>();
			log->logString = "MakeSendContext() : clientAddrBufferId is RIO_INVALID_BUFFERID";
			Logger::GetInstance().WriteLog(log);
			contextPool.Free(context);
			return nullptr;
		}
	}

	if (memcpy_s(context->clientAddrBuffer, sizeof(context->clientAddrBuffer), &session.clientSockAddrInet, sizeof(SOCKADDR_INET)) != NOERROR)
	{
		return nullptr;
	}
	context->clientAddrRIOBuffer.Length = sizeof(context->clientAddrBuffer);
	context->clientAddrRIOBuffer.Offset = 0;

	return context;
}

bool MultiSocketRUDPCore::TryRIOSend(OUT RUDPSession& session, IOContext* context)
{
	context->session = &session;

	if (rioFunctionTable.RIOSendEx(session.rioRQ, static_cast<PRIO_BUF>(context), 1, nullptr, &context->clientAddrRIOBuffer, nullptr, 0, 0, context) == false)
	{
		const auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = std::format("RIOSendEx() failed with {}", WSAGetLastError());
		Logger::GetInstance().WriteLog(log);
		contextPool.Free(context);
		return false;
	}

	return true;
}

unsigned int MultiSocketRUDPCore::MakeSendStream(OUT RUDPSession& session, OUT IOContext* context, const ThreadIdType threadId)
{
	std::set<MultiSocketRUDP::PacketSequenceSetKey> packetSequenceSet;

	unsigned int totalSendSize = 0;
	const int bufferCount = session.sendBuffer.sendPacketInfoQueue.GetRestSize();

	if (session.sendBuffer.reservedSendPacketInfo != nullptr)
	{
		if (ReservedSendPacketInfoToStream(session, packetSequenceSet, totalSendSize, threadId) == SEND_PACKET_INFO_TO_STREAM_RETURN::OCCURED_ERROR)
		{
			return 0;
		}
	}

	for (int i = 0; i < bufferCount; ++i)
	{
		switch (StoredSendPacketInfoToStream(session, packetSequenceSet, totalSendSize, threadId))
		{
		case SEND_PACKET_INFO_TO_STREAM_RETURN::OCCURED_ERROR:
		{
			return 0;
		}
		case SEND_PACKET_INFO_TO_STREAM_RETURN::STREAM_IS_FULL:
		{
			return totalSendSize;
		}
		default:
			break;
		}
	}

	return totalSendSize;
}

SEND_PACKET_INFO_TO_STREAM_RETURN MultiSocketRUDPCore::ReservedSendPacketInfoToStream(OUT RUDPSession& session, OUT std::set<MultiSocketRUDP::PacketSequenceSetKey>& packetSequenceSet, OUT unsigned int& totalSendSize, const ThreadIdType threadId)
{
	SendPacketInfo* sendPacketInfo = session.sendBuffer.reservedSendPacketInfo;
	const unsigned int useSize = sendPacketInfo->buffer->GetAllUseSize();
	if (useSize < MAX_SEND_BUFFER_SIZE)
	{
		const auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = std::format("MakeSendStream() : useSize over with {}", MAX_SEND_BUFFER_SIZE);
		Logger::GetInstance().WriteLog(log);
		PushToDisconnectTargetSession(session);
		SetEvent(sessionReleaseEventHandle);

		return SEND_PACKET_INFO_TO_STREAM_RETURN::OCCURED_ERROR;
	}

	if (not RefreshRetransmissionSendPacketInfo(sendPacketInfo, threadId))
	{
		return SEND_PACKET_INFO_TO_STREAM_RETURN::IS_ERASED_PACKET;
	}

	char* bufferPositionPointer = session.sendBuffer.rioSendBuffer;
	memcpy_s(bufferPositionPointer, MAX_SEND_BUFFER_SIZE, sendPacketInfo->buffer->GetBufferPtr(), useSize);
	packetSequenceSet.insert(MultiSocketRUDP::PacketSequenceSetKey{ sendPacketInfo->isReplyType, sendPacketInfo->sendPacketSequence });

	totalSendSize += static_cast<int>(useSize);
	if (sendPacketInfo->isReplyType == true)
	{
		sendPacketInfoPool->Free(session.sendBuffer.reservedSendPacketInfo);
	}

	session.sendBuffer.reservedSendPacketInfo = nullptr;

	return SEND_PACKET_INFO_TO_STREAM_RETURN::SUCCESS;
}

SEND_PACKET_INFO_TO_STREAM_RETURN MultiSocketRUDPCore::StoredSendPacketInfoToStream(OUT RUDPSession& session, OUT std::set<MultiSocketRUDP::PacketSequenceSetKey>& packetSequenceSet, OUT unsigned int& totalSendSize, const ThreadIdType threadId)
{
	SendPacketInfo* sendPacketInfo;

	session.sendBuffer.sendPacketInfoQueue.Dequeue(&sendPacketInfo);
	MultiSocketRUDP::PacketSequenceSetKey key{ sendPacketInfo->isReplyType, sendPacketInfo->sendPacketSequence };
	if (packetSequenceSet.contains(key) == true)
	{
		return SEND_PACKET_INFO_TO_STREAM_RETURN::IS_SENT;
	}

	const unsigned int useSize = sendPacketInfo->buffer->GetAllUseSize();
	if (useSize > MAX_SEND_BUFFER_SIZE)
	{
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = std::format("MakeSendStream() : useSize over with {}", MAX_SEND_BUFFER_SIZE);
		Logger::GetInstance().WriteLog(log);
		PushToDisconnectTargetSession(session);
		SetEvent(sessionReleaseEventHandle);

		return SEND_PACKET_INFO_TO_STREAM_RETURN::OCCURED_ERROR;
	}

	const unsigned int beforeSendSize = totalSendSize;
	totalSendSize += useSize;
	if (totalSendSize >= MAX_SEND_BUFFER_SIZE)
	{
		session.sendBuffer.reservedSendPacketInfo = sendPacketInfo;
		return SEND_PACKET_INFO_TO_STREAM_RETURN::STREAM_IS_FULL;
	}

	if (not RefreshRetransmissionSendPacketInfo(sendPacketInfo, threadId))
	{
		return SEND_PACKET_INFO_TO_STREAM_RETURN::IS_ERASED_PACKET;
	}

	packetSequenceSet.insert(key);
	memcpy_s(&session.sendBuffer.rioSendBuffer[beforeSendSize], MAX_SEND_BUFFER_SIZE - beforeSendSize, sendPacketInfo->buffer->GetBufferPtr(), useSize);
	if (sendPacketInfo->isReplyType == true)
	{
		sendPacketInfoPool->Free(sendPacketInfo);
	}

	return SEND_PACKET_INFO_TO_STREAM_RETURN::SUCCESS;
}

bool MultiSocketRUDPCore::RefreshRetransmissionSendPacketInfo(OUT SendPacketInfo* sendPacketInfo, const ThreadIdType threadId)
{
	if (sendPacketInfo->isReplyType == true)
	{
		return true;
	}

	sendPacketInfo->retransmissionTimeStamp = GetTickCount64() + retransmissionMs;
	{
		std::scoped_lock lock(*sendPacketInfoListLock[threadId]);
		if (sendPacketInfo->isErasedPacketInfo == true)
		{
			sendPacketInfo = nullptr;
			return false;
		}

		if (sendPacketInfo->retransmissionCount > 0)
		{
			sendPacketInfoList[threadId].erase(sendPacketInfo->listItor);
		}
		sendPacketInfo->listItor = sendPacketInfoList[threadId].emplace(sendPacketInfoList[threadId].end(), sendPacketInfo);
	}

	return true;
}

WORD MultiSocketRUDPCore::GetPayloadLength(OUT const NetBuffer& buffer)
{
	static constexpr int payloadLengthPosition = 1;

	return *reinterpret_cast<WORD*>(&buffer.m_pSerializeBuffer[payloadLengthPosition]);
}

void MultiSocketRUDPCore::EncodePacket(OUT NetBuffer& packet)
{
	if (packet.m_bIsEncoded == false)
	{
		packet.m_iWriteLast = packet.m_iWrite;
		packet.m_iWrite = 0;
		packet.m_iRead = 0;
		packet.Encode();
	}
}
