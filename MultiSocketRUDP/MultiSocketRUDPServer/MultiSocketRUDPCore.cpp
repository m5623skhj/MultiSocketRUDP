#include "PreCompile.h"
#include "MultiSocketRUDPCore.h"
#include "BuildConfig.h"
#include "EssentialHandler.h"
#include "LogExtension.h"
#include "Logger.h"
#include <unordered_set>

void IOContext::InitContext(SessionIdType inOwnerSessionId, RIO_OPERATION_TYPE inIOType)
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
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = "Option file read failed";
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	if (not EssentialHandlerManager::GetInst().IsRegisteredAllEssentialHandler())
	{
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = "Required handler not registered";
		Logger::GetInstance().WriteLog(log);
		EssentialHandlerManager::GetInst().PrintUnregisteredEssentialHandler();
		return false;
	}

	if (not InitNetwork())
	{
		CloseAllSessions();
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = "InitNetwork failed";
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	if (not InitRIO())
	{
		CloseAllSessions();
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = "InitRIO failed";
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	if (not RunAllThreads())
	{
		CloseAllSessions();
		StopServer();
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = "RunAllThreads() failed";
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	return true;
}

void MultiSocketRUDPCore::StopServer()
{
	threadStopFlag = true;
	const std::thread::id nowThreadId = std::this_thread::get_id();

#if USE_IOCP_SESSION_BROKER
	sessionBroker.Stop();
#else
	StopThread(sessionBrokerThread, nowThreadId);
#endif
	CloseAllSessions();
	delete[] rioCQList;

	for (ThreadIdType i = 0; i < numOfWorkerThread; ++i)
	{
		SetEvent(recvLogicThreadEventHandles[i]);
		StopThread(ioWorkerThreads[i], nowThreadId);
		StopThread(recvLogicWorkerThreads[i], nowThreadId);
		StopThread(retransmissionThreads[i], nowThreadId);
	}

	SetEvent(sessionReleaseEventHandle);
	StopThread(sessionReleaseThread, nowThreadId);

	Logger::GetInstance().StopLoggerThread();

	isServerStopped = true;
	auto log = Logger::MakeLogObject<ServerLog>();
	log->logString = "Server stop";
	Logger::GetInstance().WriteLog(log);
}

void MultiSocketRUDPCore::StopThread(std::thread& stopTarget, const std::thread::id& threadId)
{
	if (stopTarget.joinable() && threadId != stopTarget.get_id())
	{
		stopTarget.join();
	}
}

bool MultiSocketRUDPCore::SendPacket(SendPacketInfo* sendPacketInfo)
{
	auto buffer = sendPacketInfo->GetBuffer();

	if (buffer->m_bIsEncoded == false)
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

	auto log = Logger::MakeLogObject<ServerLog>();
	log->logString = std::format("Session id {} is disconnected", disconnectTargetSessionId);
	Logger::GetInstance().WriteLog(log);
}

void MultiSocketRUDPCore::EraseSendPacketInfo(OUT SendPacketInfo* eraseTarget, const ThreadIdType threadId)
{
	if (eraseTarget == nullptr)
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
			std::scoped_lock lock(*sendedPacketInfoListLock[threadId]);
			sendedPacketInfoList[threadId].erase(eraseTarget->listItor);
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
	int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != 0)
	{
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = std::format("WSAStartup failed {}", result);
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	sessionArray.reserve(numOfSockets);
	for (auto socketNumber = 0; socketNumber < numOfSockets; ++socketNumber)
	{
		auto optSocket = CreateRUDPSocket(socketNumber);
		if (not optSocket.has_value())
		{
			WSACleanup();
			return false;
		}

		sessionArray.emplace_back(new RUDPSession(optSocket.value(), static_cast<PortType>(portStartNumber + socketNumber), *this));
		sessionArray[socketNumber]->sessionId = socketNumber;
	}

	return true;
}

bool MultiSocketRUDPCore::InitRIO()
{
	GUID guid = WSAID_MULTIPLE_RIO;
	DWORD bytes = 0;

	// For the purpose of obtaining the function table, any of the created sessions selected
	if (WSAIoctl((sessionArray[0])->sock, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER, &guid, sizeof(GUID)
		, reinterpret_cast<void**>(&rioFunctionTable), sizeof(rioFunctionTable), &bytes, NULL, NULL))
	{
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = std::format("WSAIoctl_SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER with {}", WSAGetLastError());
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	rioCQList = new RIO_CQ[numOfWorkerThread];
	if (rioCQList == nullptr)
	{
		return false;
	}
	for (int rioCQIndex = 0; rioCQIndex < numOfWorkerThread; ++rioCQIndex)
	{
		rioCQList[rioCQIndex] = rioFunctionTable.RIOCreateCompletionQueue(numOfSockets / numOfWorkerThread * maxSendBufferSize, nullptr);
	}

	for (auto& session : sessionArray)
	{
		session->threadId = session->sessionId % numOfWorkerThread;
		if (not session->InitializeRIO(rioFunctionTable, rioCQList[session->threadId], rioCQList[session->threadId]))
		{
			return false;
		}

		if (not InitSessionRecvBuffer(session))
		{
			return false;
		}

		if (not DoRecv(*session))
		{
			return false;
		}

		unusedSessionIdList.emplace_back(session->sessionId);
	}

	return true;
}

bool MultiSocketRUDPCore::InitSessionRecvBuffer(RUDPSession* session)
{
	session->recvBuffer.recvContext = std::make_shared<IOContext>();
	if (session->recvBuffer.recvContext == nullptr)
	{
		return false;
	}

	auto& context = session->recvBuffer.recvContext;
	context->InitContext(session->sessionId, RIO_OPERATION_TYPE::OP_RECV);
	context->Length = recvBufferSize;
	context->Offset = 0;
	context->session = session;

	context->clientAddrRIOBuffer.Length = sizeof(SOCKADDR_INET);
	context->clientAddrRIOBuffer.Offset = 0;

	context->localAddrRIOBuffer.Length = sizeof(SOCKADDR_INET);
	context->localAddrRIOBuffer.Offset = 0;

	context->BufferId = RegisterRIOBuffer(session->recvBuffer.buffer, recvBufferSize);
	context->clientAddrRIOBuffer.BufferId = RegisterRIOBuffer(context->clientAddrBuffer, sizeof(SOCKADDR_INET));
	context->localAddrRIOBuffer.BufferId = RegisterRIOBuffer(context->localAddrBuffer, sizeof(SOCKADDR_INET));

	if (context->BufferId == RIO_INVALID_BUFFERID || context->clientAddrRIOBuffer.BufferId == RIO_INVALID_BUFFERID || context->localAddrRIOBuffer.BufferId == RIO_INVALID_BUFFERID)
	{
		return false;
	}

	return true;
}

RIO_BUFFERID MultiSocketRUDPCore::RegisterRIOBuffer(char* targetBuffer, const unsigned int targetBuffersize) const
{
	if (targetBuffer == nullptr)
	{
		return RIO_INVALID_BUFFERID;
	}

	RIO_BUFFERID bufferId = rioFunctionTable.RIORegisterBuffer(targetBuffer, targetBuffersize);
	if (bufferId == RIO_INVALID_BUFFERID)
	{
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = std::format("RIORegisterBuffer failed with error code {}", WSAGetLastError());
		Logger::GetInstance().WriteLog(log);
	}

	return bufferId;
}

bool MultiSocketRUDPCore::RunAllThreads()
{
	sendedPacketInfoList.reserve(numOfWorkerThread);
	sendedPacketInfoListLock.reserve(numOfWorkerThread);
	ioWorkerThreads.reserve(numOfWorkerThread);
	recvLogicWorkerThreads.reserve(numOfWorkerThread);
	retransmissionThreads.reserve(numOfWorkerThread);

	logicThreadEventStopHandle = CreateEvent(NULL, TRUE, FALSE, NULL);
	sessionReleaseEventHandle = CreateEvent(NULL, TRUE, FALSE, NULL);
	recvLogicThreadEventHandles.reserve(numOfWorkerThread);
	ioCompletedContexts.reserve(numOfWorkerThread);

	sessionReleaseThread = std::thread([this]() { RunSessionReleaseThread(); });
	for (unsigned char id = 0; id < numOfWorkerThread; ++id)
	{
		ioCompletedContexts.emplace_back();

		recvLogicThreadEventHandles.emplace_back(CreateEvent(NULL, FALSE, FALSE, NULL));
		sendedPacketInfoList.emplace_back();
		sendedPacketInfoListLock.push_back(std::make_unique<std::mutex>());

		ioWorkerThreads.emplace_back([this, id]() { this->RunWorkerThread(static_cast<ThreadIdType>(id)); });
		recvLogicWorkerThreads.emplace_back([this, id]() { this->RunRecvLogicWorkerThread(static_cast<ThreadIdType>(id)); });
		retransmissionThreads.emplace_back([this, id]() { this->RunRetransmissionThread(static_cast<ThreadIdType>(id)); });
	}

	Sleep(1000);
	if (not RunSessionBroker())
	{
		auto log = Logger::MakeLogObject<ServerLog>();
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
	sessionBrokerThread = std::thread([this]() { this->RunSessionBrokerThread(sessionBrokerPort, coreServerIp); });
#endif

	return true;
}

std::optional<SOCKET> MultiSocketRUDPCore::CreateRUDPSocket(const unsigned short socketNumber) const
{
	SOCKET sock = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, WSA_FLAG_REGISTERED_IO);
	if (sock == INVALID_SOCKET)
	{
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = std::format("Socket create failed {}", WSAGetLastError());
		Logger::GetInstance().WriteLog(log);
		return std::nullopt;
	}

	sockaddr_in serverAddr;
	memset(&serverAddr, 0, sizeof(serverAddr));

	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.S_un.S_addr = INADDR_ANY;
	serverAddr.sin_port = htons(portStartNumber + socketNumber);

	if (bind(sock, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
	{
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = std::format("Bind failed {}", WSAGetLastError());
		Logger::GetInstance().WriteLog(log);
		closesocket(sock);
		return std::nullopt;
	}

	return sock;
}

void MultiSocketRUDPCore::CloseAllSessions()
{
	{
		std::scoped_lock lock(unusedSessionIdListLock);
		unusedSessionIdList.clear();
	}

	{
		for (auto& session : sessionArray)
		{
			closesocket(session->sock);
			delete session;
		}

		// need wait?
		sessionArray.clear();
		connectedUserCount = 0;
	}
}

RUDPSession* MultiSocketRUDPCore::AcquireSession()
{
	RUDPSession* session = nullptr;
	SessionIdType sessionId{};
	{
		std::scoped_lock lock(unusedSessionIdListLock);

		if (unusedSessionIdList.empty() == true)
		{
			return nullptr;
		}

		sessionId = unusedSessionIdList.front();
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

void MultiSocketRUDPCore::RunWorkerThread(const ThreadIdType threadId)
{
	RIORESULT rioResults[maxRIOResult];
	ULONG numOfResults = 0;

	TickSet tickSet;
	tickSet.nowTick = GetTickCount64();
	tickSet.beforeTick = tickSet.nowTick;

	while (not threadStopFlag)
	{
		ZeroMemory(rioResults, sizeof(rioResults));

		numOfResults = rioFunctionTable.RIODequeueCompletion(rioCQList[threadId], rioResults, maxRIOResult);
		for (ULONG i = 0; i < numOfResults; ++i)
		{
			auto context = GetIOCompletedContext(rioResults[i]);
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

	auto log = Logger::MakeLogObject<ServerLog>();
	log->logString = "Worker thread stopped";
	Logger::GetInstance().WriteLog(log);
}

void MultiSocketRUDPCore::RunRecvLogicWorkerThread(const ThreadIdType threadId)
{
	HANDLE eventHandles[2] = { recvLogicThreadEventHandles[threadId], logicThreadEventStopHandle };
	while (not threadStopFlag)
	{
		const auto waitResult = WaitForMultipleObjects(2, eventHandles, FALSE, INFINITE);
		switch (waitResult)
		{
		case WAIT_OBJECT_0:
		{
			OnRecvPacket(threadId);
		}
		break;
		case WAIT_OBJECT_0 + 1:
		{
			Sleep(logicThreadStopSleepTime);
			OnRecvPacket(threadId);
			auto log = Logger::MakeLogObject<ServerLog>();
			log->logString = std::format("Logic thread stop. ThreadId is {}", threadId);
			Logger::GetInstance().WriteLog(log);
			break;
		}
		break;
		default:
		{
			auto log = Logger::MakeLogObject<ServerLog>();
			log->logString = std::format("Invalid logic thread wait result. Error is {}", WSAGetLastError());
			Logger::GetInstance().WriteLog(log);
		}
		break;
		}
	}
}

void MultiSocketRUDPCore::RunRetransmissionThread(const ThreadIdType threadId)
{
	TickSet tickSet;
	tickSet.nowTick = GetTickCount64();
	tickSet.beforeTick = tickSet.nowTick;

	auto& thisThreadSendedPacketInfoList = sendedPacketInfoList[threadId];
	auto& thisThreadSendedPacketInfoListLock = *sendedPacketInfoListLock[threadId];

	std::list<SendPacketInfo*> copyList;
	unsigned short numOfTimeoutSession{};

	while (not threadStopFlag)
	{
		{
			std::scoped_lock lock(thisThreadSendedPacketInfoListLock);
			copyList.assign(thisThreadSendedPacketInfoList.begin(), thisThreadSendedPacketInfoList.end());
		}
		
		for (auto& sendedPacketInfo : copyList)
		{
			if (sendedPacketInfo->sendTimeStamp < tickSet.nowTick || sendedPacketInfo->owner->nowInReleaseThread || sendedPacketInfo->isErasedPacketInfo)
			{
				continue;
			}

			if (++sendedPacketInfo->retransmissionCount >= maxPacketRetransmissionCount)
			{
				PushToDisconnectTargetSession(*sendedPacketInfo->owner);
				++numOfTimeoutSession;
				continue;
			}

			SendPacket(sendedPacketInfo);
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
		const auto waitResult = WaitForSingleObject(sessionReleaseEventHandle, INFINITE);
		switch (waitResult)
		{
		case WAIT_OBJECT_0:
		{
			std::scoped_lock lock(releaseSessionIdListLock);
			for (auto& releaseSessionId : releaseSessionIdList)
			{
				if (auto releaseSession = GetUsingSession(releaseSessionId))
				{
					if (releaseSession->sendBuffer.ioMode == IO_MODE::IO_SENDING)
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

void MultiSocketRUDPCore::SleepRemainingFrameTime(OUT TickSet& tickSet, const unsigned int intervalMs)
{
	tickSet.nowTick = GetTickCount64();
	UINT64 deltaTick = tickSet.nowTick - tickSet.beforeTick;

	if (deltaTick < intervalMs && deltaTick > 0)
	{
		Sleep(intervalMs - static_cast<DWORD>(deltaTick));
	}

	tickSet.beforeTick = tickSet.nowTick;
}

IOContext* MultiSocketRUDPCore::GetIOCompletedContext(RIORESULT& rioResult)
{
	IOContext* context = reinterpret_cast<IOContext*>(rioResult.RequestContext);
	if (context == nullptr)
	{
		return nullptr;
	}

	context->session = GetUsingSession(context->ownerSessionId);
	if (context->session == nullptr)
	{
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = std::format("Session is nullptr in GetIOCompletedContext() with session id {}, error {}", context->ownerSessionId, rioResult.Status);
		Logger::GetInstance().WriteLog(log);
		contextPool.Free(context);
		return nullptr;
	}

	if (rioResult.BytesTransferred == 0 || context->session->ioCancle == true)
	{
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = std::format("RIO operation failed with session id {}, error {}", context->ownerSessionId, rioResult.Status);
		Logger::GetInstance().WriteLog(log);
		contextPool.Free(context);
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
	break;
	case RIO_OPERATION_TYPE::OP_SEND:
	{
		return SendIOCompleted(*contextResult->session, threadId);
	}
	break;
	default:
	{
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = std::format("Invalid rio operation type. Type is {}", static_cast<unsigned char>(contextResult->ioType));
		Logger::GetInstance().WriteLog(log);
	}
	break;
	}

	return false;
}

bool MultiSocketRUDPCore::RecvIOCompleted(OUT IOContext* contextResult, const ULONG transferred, const BYTE threadId)
{
	auto buffer = NetBuffer::Alloc();
	if (memcpy_s(buffer->m_pSerializeBuffer, recvBufferSize, contextResult->session->recvBuffer.buffer, transferred) != 0)
	{
		return false;
	}
	buffer->m_iWrite = static_cast<WORD>(transferred);

	contextResult->session->recvBuffer.recvBufferList.Enqueue(buffer);
	ioCompletedContexts[threadId].Enqueue(contextResult);
	SetEvent(recvLogicThreadEventHandles[threadId]);

	return DoRecv(*contextResult->session);
}

bool MultiSocketRUDPCore::SendIOCompleted(RUDPSession& session, const BYTE threadId)
{
	InterlockedExchange((UINT*)&session.sendBuffer.ioMode, (UINT)IO_MODE::IO_NONE_SENDING);
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

bool MultiSocketRUDPCore::DoRecv(RUDPSession& session)
{
	auto context = session.recvBuffer.recvContext;
	if (context == nullptr)
	{
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = "DoRecv() : context is nullptr";
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	if (rioFunctionTable.RIOReceiveEx(session.rioRQ, context.get(), 1, &context->localAddrRIOBuffer, &context->clientAddrRIOBuffer, NULL, NULL, 0, context.get()) == false)
	{
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = std::format("RIOReceiveEx() failed with {}", WSAGetLastError());
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	return true;
}

bool MultiSocketRUDPCore::DoSend(OUT RUDPSession& session, const ThreadIdType threadId)
{
	while (1)
	{
		if (InterlockedCompareExchange((UINT*)&session.sendBuffer.ioMode, (UINT)IO_MODE::IO_SENDING, (UINT)IO_MODE::IO_NONE_SENDING))
		{
			break;
		}

		if (session.sendBuffer.sendPacketInfoQueue.GetRestSize() == 0 &&
			session.sendBuffer.reservedSendPacketInfo == nullptr)
		{
			InterlockedExchange((UINT*)&session.sendBuffer.ioMode, (UINT)IO_MODE::IO_NONE_SENDING);
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
			auto log = Logger::MakeLogObject<ServerLog>();
			log->logString = "MakeSendContext() : clientAddrBufferId is RIO_INVALID_BUFFERID";
			Logger::GetInstance().WriteLog(log);
			contextPool.Free(context);
			return nullptr;
		}
	}

	if (memcpy_s(context->clientAddrBuffer, sizeof(context->clientAddrBuffer), &session.clientSockaddrInet, sizeof(SOCKADDR_INET)) != NOERROR)
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

	if (rioFunctionTable.RIOSendEx(session.rioRQ, static_cast<PRIO_BUF>(context), 1, NULL, &context->clientAddrRIOBuffer, nullptr, 0, 0, context) == false)
	{
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = std::format("RIOSendEx() failed with {}", WSAGetLastError());
		Logger::GetInstance().WriteLog(log);
		contextPool.Free(context);
		return false;
	}

	return true;
}

int MultiSocketRUDPCore::MakeSendStream(OUT RUDPSession& session, OUT IOContext* context, const ThreadIdType threadId)
{
	std::unordered_set<PacketSequence> packetSequenceSet;

	int totalSendSize = 0;
	int bufferCount = session.sendBuffer.sendPacketInfoQueue.GetRestSize();
	char* bufferPositionPointer = session.sendBuffer.rioSendBuffer;

	if (session.sendBuffer.reservedSendPacketInfo != nullptr)
	{
		int useSize = session.sendBuffer.reservedSendPacketInfo->buffer->GetAllUseSize();
		if (useSize < maxSendBufferSize)
		{
			auto log = Logger::MakeLogObject<ServerLog>();
			log->logString = std::format("MakeSendStream() : useSize over with {}", maxSendBufferSize);
			Logger::GetInstance().WriteLog(log);
			PushToDisconnectTargetSession(session);
			SetEvent(sessionReleaseEventHandle);
			return 0;
		}

		memcpy_s(bufferPositionPointer, maxSendBufferSize
			, session.sendBuffer.reservedSendPacketInfo->buffer->GetBufferPtr(), useSize);
		packetSequenceSet.insert(session.sendBuffer.reservedSendPacketInfo->sendPacektSequence);

		totalSendSize += useSize;
		bufferPositionPointer += totalSendSize;
		session.sendBuffer.reservedSendPacketInfo = nullptr;
	}

	SendPacketInfo* sendPacketInfo;
	for (int i = 0; i < bufferCount; ++i)
	{
		session.sendBuffer.sendPacketInfoQueue.Dequeue(&sendPacketInfo);
		if (packetSequenceSet.contains(sendPacketInfo->sendPacektSequence) == true)
		{
			continue;
		}

		int useSize = sendPacketInfo->buffer->GetAllUseSize();
		if (useSize > maxSendBufferSize)
		{
			auto log = Logger::MakeLogObject<ServerLog>();
			log->logString = std::format("MakeSendStream() : useSize over with {}", maxSendBufferSize);
			Logger::GetInstance().WriteLog(log);
			PushToDisconnectTargetSession(session);
			SetEvent(sessionReleaseEventHandle);
			return 0;
		}

		totalSendSize += useSize;
		if (totalSendSize >= maxSendBufferSize)
		{
			session.sendBuffer.reservedSendPacketInfo = sendPacketInfo;
			break;
		}

		sendPacketInfo->sendTimeStamp = GetTickCount64() + retransmissionMs;
		{
			std::scoped_lock lock(*sendedPacketInfoListLock[threadId]);
			if (sendPacketInfo->isErasedPacketInfo == true)
			{
				continue;
			}

			if (sendPacketInfo->retransmissionCount > 0)
			{
				sendedPacketInfoList[threadId].erase(sendPacketInfo->listItor);
			}
			sendPacketInfo->listItor = sendedPacketInfoList[threadId].emplace(sendedPacketInfoList[threadId].end(), sendPacketInfo);
		}
		packetSequenceSet.insert(sendPacketInfo->sendPacektSequence);
		memcpy_s(&session.sendBuffer.rioSendBuffer[totalSendSize - useSize], maxSendBufferSize - totalSendSize - useSize
			, sendPacketInfo->buffer->GetBufferPtr(), useSize);
	}

	return totalSendSize;
}

WORD MultiSocketRUDPCore::GetPayloadLength(OUT NetBuffer& buffer) const
{
	static constexpr int payloadLengthPosition = 1;

	return *((WORD*)(&buffer.m_pSerializeBuffer[payloadLengthPosition]));
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
