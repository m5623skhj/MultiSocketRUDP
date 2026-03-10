#include "PreCompile.h"
#include "MultiSocketRUDPCore.h"
#include "BuildConfig.h"
#include "LogExtension.h"
#include "Logger.h"
#include "Ticker.h"
#include "MemoryTracer.h"
#include "MultiSocketRUDPCoreFuntionDeletage.h"
#include "RIOManager.h"
#include "RUDPSessionBroker.h"
#include "SendPacketInfo.h"
#include "../Common/etc/UtilFunc.h"
#include "RUDPSessionManager.h"
#include "RUDPThreadManager.h"
#include "RUDPPacketProcessor.h"
#include "RUDPIOHandler.h"

namespace
{
	FORCEINLINE void SleepRemainingFrameTime(OUT TickSet& tickSet, const unsigned int intervalMs)
	{
		const UINT64 now = GetTickCount64();
		if (const UINT64 delta = now - tickSet.nowTick; delta < intervalMs)
		{
			Sleep(static_cast<DWORD>(intervalMs - delta));
		}

		tickSet.nowTick = GetTickCount64();
	}

	SOCKET CreateRUDPSocket()
	{
		const SOCKET sock = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_REGISTERED_IO);
		if (sock == INVALID_SOCKET)
		{
			LOG_ERROR(std::format("WSASocket failed with error code {}", WSAGetLastError()));
			return INVALID_SOCKET;
		}

		sockaddr_in serverAddr = {};
		serverAddr.sin_family = AF_INET;
		serverAddr.sin_addr.S_un.S_addr = INADDR_ANY;
		serverAddr.sin_port = 0;

		if (bind(sock, reinterpret_cast<SOCKADDR*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR)
		{
			LOG_ERROR(std::format("Bind failed with error code {}", WSAGetLastError()));
			closesocket(sock);
			return INVALID_SOCKET;
		}

		socklen_t len = sizeof(serverAddr);
		getsockname(sock, reinterpret_cast<sockaddr*>(&serverAddr), &len);

		return sock;
	}
}

MultiSocketRUDPCore::MultiSocketRUDPCore(std::wstring&& inSessionBrokerCertStoreName
	, std::wstring&& inSessionBrokerCertSubjectName)
	: sessionBrokerCertStoreName(std::move(inSessionBrokerCertStoreName))
	, sessionBrokerCertSubjectName(std::move(inSessionBrokerCertSubjectName))
	, recvIOCompletedContextPool(2, false)
	, contextPool(2, false)
{
}

MultiSocketRUDPCore::~MultiSocketRUDPCore()
{
}

bool MultiSocketRUDPCore::StartServer(const std::wstring& coreOptionFilePath, const std::wstring& sessionBrokerOptionFilePath, SessionFactoryFunc&& factoryFunc, const bool printLogToConsole)
{
	Logger::GetInstance().RunLoggerThread(printLogToConsole);

	if (not ReadOptionFile(coreOptionFilePath, sessionBrokerOptionFilePath))
	{
		LOG_ERROR("Option file read failed");
		return false;
	}

	MultiSocketRUDPCoreFunctionDelegate::Instance().Init(*this);
	if (not InitNetwork())
	{
		StopServer();
		LOG_ERROR("InitNetwork failed");
		return false;
	}

	sessionManager = std::make_unique<RUDPSessionManager>(numOfSockets, *this, sessionDelegate);
	if (sessionManager == nullptr)
	{
		LOG_ERROR("Session manager creation failed");
		return false;
	}

	if (not sessionManager->Initialize(numOfWorkerThread, std::move(factoryFunc)))
	{
		LOG_ERROR("Session factory function is not set");
		return false;
	}

	if (not InitRIO())
	{
		StopServer();
		LOG_ERROR("InitRIO failed");
		return false;
	}

	if (not RunAllThreads())
	{
		StopServer();
		LOG_ERROR("RunAllThreads failed");
		return false;
	}

	return true;
}

void MultiSocketRUDPCore::StopServer()
{
	threadStopFlag = true;

	sessionBroker->Stop();
	CloseAllSessions();

	SetEvent(recvLogicThreadEventStopHandle);
	SetEvent(sessionReleaseStopEventHandle);
	StopAllThreads();

	for (auto const handle : recvLogicThreadEventHandles)
	{
		CloseHandle(handle);
	}
	CloseHandle(recvLogicThreadEventStopHandle);
	CloseHandle(sessionReleaseEventHandle);
	CloseHandle(sessionReleaseStopEventHandle);

	Ticker::GetInstance().Stop();

	ClearAllSession();

	Logger::GetInstance().StopLoggerThread();

	WSACleanup();
	isServerStopped = true;
	const auto log = Logger::MakeLogObject<ServerLog>();
	log->logString = "Server stop";
	Logger::GetInstance().WriteLog(log);
}

bool MultiSocketRUDPCore::IsServerStopped() const
{
	return isServerStopped;
}

unsigned short MultiSocketRUDPCore::GetConnectedUserCount() const
{
	return sessionManager->GetConnectedCount();
}

bool MultiSocketRUDPCore::SendPacket(SendPacketInfo* sendPacketInfo, const bool needAddRefCount) const
{
	if (sendPacketInfo == nullptr || sendPacketInfo->owner == nullptr || sendPacketInfo->GetBuffer() == nullptr)
	{
		LOG_ERROR("SendPacketInfo or its owner or its buffer is nullptr in MultiSocketRUDPCore::SendPacket()");
		return false;
	}

	if (sendPacketInfo->owner->nowInReleaseThread)
	{
		return false;
	}

	if (needAddRefCount)
	{
		sendPacketInfo->AddRefCount();
	}

	if (sendPacketInfo->retransmissionCount == 0)
	{
		NetBuffer* buffer = sendPacketInfo->GetBuffer();
		buffer->m_iWriteLast = buffer->m_iWrite;
		buffer->m_iWrite = 0;
		buffer->m_iRead = 0;
	}

	sendPacketInfo->owner->rioContext.GetSendContext().PushSendPacketInfo(sendPacketInfo);
	if (not ioHandler->DoSend(*sendPacketInfo->owner, sendPacketInfo->owner->threadId))
	{
		return false;
	}

	return true;
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

	SendPacketInfo::Free(eraseTarget);
}

RIO_EXTENSION_FUNCTION_TABLE MultiSocketRUDPCore::GetRIOFunctionTable() const
{
	return rioManager->GetRIOFunctionTable();
}

int32_t MultiSocketRUDPCore::GetTPS() const
{
	return packetProcessor->GetTPS();
}

void MultiSocketRUDPCore::ResetTPS() const
{
	packetProcessor->ResetTPS();
}

void MultiSocketRUDPCore::DisconnectSession(const SessionIdType disconnectTargetSessionId) const
{
	if (not sessionManager->ReleaseSession(disconnectTargetSessionId))
	{
		return;
	}

	const auto log = Logger::MakeLogObject<ServerLog>();
	log->logString = std::format("Session id {} is disconnected", disconnectTargetSessionId);
	Logger::GetInstance().WriteLog(log);
}

void MultiSocketRUDPCore::PushToDisconnectTargetSession(RUDPSession& session)
{
	std::scoped_lock lock(releaseSessionIdListLock);
	session.nowInReleaseThread = true;
	releaseSessionIdList.emplace_back(session.GetSessionId());
	SetEvent(sessionReleaseEventHandle);
}

void MultiSocketRUDPCore::EnqueueContextResult(const IOContext* contextResult, const BYTE threadId)
{
	if (contextResult == nullptr)
	{
		LOG_ERROR("ContextResult is nullptr in EnqueueContextResult()");
		return;
	}

	const auto recvIOContext = recvIOCompletedContextPool.Alloc();
	if (recvIOContext == nullptr)
	{
		LOG_ERROR("recvIOContext is nullptr in EnqueueContextResult()");
		DisconnectSession(contextResult->ownerSessionId);
	}

	recvIOContext->InitContext(contextResult->session, contextResult->clientAddrBuffer);
	recvIOCompletedContexts[threadId].Enqueue(recvIOContext);
	ReleaseSemaphore(recvLogicThreadEventHandles[threadId], 1, nullptr);
}

bool MultiSocketRUDPCore::InitNetwork() const
{
	WSADATA wsaData;
	if (int result = WSAStartup(MAKEWORD(2, 2), &wsaData); result != 0)
	{
		LOG_ERROR(std::format("WSAStartup failed with error code {}", result));
		return false;
	}

	RUDPSession::SetMaximumPacketHoldingQueueSize(maxHoldingPacketQueueSize);

	return true;
}

bool MultiSocketRUDPCore::InitRIO()
{
	bool result = true;
	do
	{
		rioManager = std::make_unique<RIOManager>(sessionDelegate);
		ioHandler = std::make_unique<RUDPIOHandler>(*rioManager, sessionDelegate, contextPool, sendPacketInfoList, sendPacketInfoListLock, maxHoldingPacketQueueSize, retransmissionMs);
		if (rioManager == nullptr || ioHandler == nullptr)
		{
			LOG_ERROR("RIOManager or RUDPIOHandler creation failed");
			result = false;
			break;
		}

		if (rioManager->Initialize(numOfSockets, numOfWorkerThread) == false)
		{
			LOG_ERROR("RIOManager initialization failed");
			result = false;
			break;
		}
	} while (false);
	
	return result;
}

bool MultiSocketRUDPCore::RunAllThreads()
{
	threadManager = std::make_unique<RUDPThreadManager>();
	packetProcessor = std::make_unique<RUDPPacketProcessor>(*sessionManager, sessionDelegate);
	if (threadManager == nullptr || packetProcessor == nullptr)
	{
		LOG_ERROR("ThreadManager or PacketProcessor creation failed");
		return false;
	}

	sendPacketInfoList.reserve(numOfWorkerThread);
	sendPacketInfoListLock.reserve(numOfWorkerThread);

	recvLogicThreadEventStopHandle = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	sessionReleaseStopEventHandle = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	sessionReleaseEventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	recvIOCompletedContexts.reserve(numOfWorkerThread);

	Ticker::GetInstance().Start(timerTickMs);
	for (unsigned char id = 0; id < numOfWorkerThread; ++id)
	{
		recvIOCompletedContexts.emplace_back();

		recvLogicThreadEventHandles.emplace_back(CreateSemaphore(nullptr, 0, LONG_MAX, nullptr));
		sendPacketInfoList.emplace_back();
		sendPacketInfoListLock.push_back(std::make_unique<std::mutex>());
	}

	threadManager->StartThreads(THREAD_GROUP::SESSION_RELEASE_THREAD, [this](const std::stop_token& stopToken, unsigned char _) { this->RunSessionReleaseThread(stopToken); }, 1);
	threadManager->StartThreads(THREAD_GROUP::HEARTBEAT_THREAD, [this](const std::stop_token& stopToken, unsigned char _) { this->RunHeartbeatThread(stopToken); }, 1);

	threadManager->StartThreads(THREAD_GROUP::IO_WORKER_THREAD, [this](const std::stop_token& stopToken, const unsigned char id) { this->RunIOWorkerThread(stopToken, id); }, numOfWorkerThread);
	threadManager->StartThreads(THREAD_GROUP::RECV_LOGIC_WORKER_THREAD, [this](const std::stop_token& stopToken, const unsigned char id) { this->RunRecvLogicWorkerThread(stopToken, id); }, numOfWorkerThread);
	threadManager->StartThreads(THREAD_GROUP::RETRANSMISSION_THREAD, [this](const std::stop_token& stopToken, const unsigned char id) { this->RunRetransmissionThread(stopToken, id); }, numOfWorkerThread);

	Sleep(1000);
	sessionBroker = std::make_unique<RUDPSessionBroker>(*this, sessionDelegate, sessionBrokerCertStoreName, sessionBrokerCertSubjectName);
	if (not sessionBroker->Start(sessionBrokerPort, coreServerIp))
	{
		LOG_ERROR("RunSessionBroker failed");
		return false;
	}

	return true;
}

void MultiSocketRUDPCore::CloseAllSessions() const
{
	sessionManager->CloseAllSessions();
}

void MultiSocketRUDPCore::ClearAllSession()
{
	{
		std::scoped_lock lock(releaseSessionIdListLock);
		releaseSessionIdList.clear();
	}

	sessionManager->ClearAllSessions();
}

void MultiSocketRUDPCore::ReleaseAllSession() const
{
	sessionManager->ClearAllSessions();
}

RUDPSession* MultiSocketRUDPCore::AcquireSession() const
{
	return sessionManager->AcquireSession();
}

RUDPSession* MultiSocketRUDPCore::GetUsingSession(const SessionIdType sessionId) const
{
	return sessionManager->GetUsingSession(sessionId);
}

RUDPSession* MultiSocketRUDPCore::GetReleasingSession(const SessionIdType sessionId) const
{
	return sessionManager->GetReleasingSession(sessionId);
}

CONNECT_RESULT_CODE MultiSocketRUDPCore::InitReserveSession(OUT RUDPSession& session) const
{
	session.socketContext.SetSocket(CreateRUDPSocket());
	const SOCKET sock = session.GetSocket();
	if (sock == INVALID_SOCKET)
	{
		LOG_ERROR(std::format("CreateRUDPSocket failed with error {}", WSAGetLastError()));
		return CONNECT_RESULT_CODE::CREATE_SOCKET_FAILED;
	}

	auto raii = Util::MakeScopeExit([&session, this]() {
		if (session.GetSocket() != INVALID_SOCKET)
		{
			session.rioContext.Cleanup(rioManager->GetRIOFunctionTable());
			session.socketContext.CloseSocket();
		}
	});

	sockaddr_in serverAddr;
	socklen_t len = sizeof(serverAddr);
	getsockname(sock, reinterpret_cast<sockaddr*>(&serverAddr), &len);
	session.socketContext.SetServerPort(ntohs(serverAddr.sin_port));

	if (not rioManager->InitializeSessionRIO(session, session.GetThreadId()))
	{
		LOG_ERROR(std::format("RUDPSession::InitializeRIO failed with error {}", WSAGetLastError()));
		return CONNECT_RESULT_CODE::RIO_INIT_FAILED;
	}

	if (not ioHandler->DoRecv(session))
	{
		LOG_ERROR(std::format("DoRecv failed with error {}", WSAGetLastError()));
		return CONNECT_RESULT_CODE::DO_RECV_FAILED;
	}
	session.stateMachine.SetReserved();

	raii.Dismiss();
	return CONNECT_RESULT_CODE::SUCCESS;
}

void MultiSocketRUDPCore::StopAllThreads() const
{
	threadManager->StopThreadGroup(THREAD_GROUP::IO_WORKER_THREAD);
	threadManager->StopThreadGroup(THREAD_GROUP::RECV_LOGIC_WORKER_THREAD);
	threadManager->StopThreadGroup(THREAD_GROUP::RETRANSMISSION_THREAD);
	threadManager->StopThreadGroup(THREAD_GROUP::SESSION_RELEASE_THREAD);
	threadManager->StopThreadGroup(THREAD_GROUP::HEARTBEAT_THREAD);
}

void MultiSocketRUDPCore::RunIOWorkerThread(const std::stop_token& stopToken, const ThreadIdType threadId)
{
	TickSet tickSet;
	tickSet.nowTick = GetTickCount64();

	while (not stopToken.stop_requested())
	{
		RIORESULT rioResults[MAX_RIO_RESULT];
		ZeroMemory(rioResults, sizeof(rioResults));

		const ULONG numOfResults = rioManager->DequeueCompletions(threadId, rioResults, MAX_RIO_RESULT);
		for (ULONG i = 0; i < numOfResults; ++i)
		{
			const auto context = GetIOCompletedContext(rioResults[i]);
			if (context == nullptr)
			{
				continue;
			}

			if (not ioHandler->IOCompleted(context, rioResults[i].BytesTransferred, threadId))
			{
				LOG_ERROR(std::format("IOCompleted() failed with io type {}", static_cast<INT8>(context->ioType)));
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

void MultiSocketRUDPCore::RunRecvLogicWorkerThread(const std::stop_token& stopToken, const ThreadIdType threadId)
{
	const HANDLE eventHandles[2] = { recvLogicThreadEventHandles[threadId], recvLogicThreadEventStopHandle };
	while (not stopToken.stop_requested())
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
			const auto log = Logger::MakeLogObject<ServerLog>();
			log->logString = std::format("Logic thread stop. ThreadId is {}", threadId);
			Logger::GetInstance().WriteLog(log);
			return;
		}
		default:
		{
			LOG_ERROR(std::format("Invalid logic thread wait result. Error is {}", WSAGetLastError()));
			break;
		}
		}
	}
}

void MultiSocketRUDPCore::RunRetransmissionThread(const std::stop_token& stopToken, const ThreadIdType threadId)
{
	TickSet tickSet;
	tickSet.nowTick = GetTickCount64();

	auto& thisThreadSendPacketInfoList = sendPacketInfoList[threadId];
	auto& thisThreadSendPacketInfoListLock = *sendPacketInfoListLock[threadId];

	std::list<SendPacketInfo*> copyList;
	unsigned short numOfTimeoutSession{};

	while (not stopToken.stop_requested())
	{
		{
			std::scoped_lock lock(thisThreadSendPacketInfoListLock);
			copyList.assign(thisThreadSendPacketInfoList.begin(), thisThreadSendPacketInfoList.end());
		}
		
		for (const auto& sendPacketInfo : copyList)
		{
			if (sendPacketInfo->retransmissionTimeStamp > tickSet.nowTick || sendPacketInfo->owner == nullptr || sendPacketInfo->owner->nowInReleaseThread || sendPacketInfo->isErasedPacketInfo)
			{
				continue;
			}

			if (++sendPacketInfo->retransmissionCount >= maxPacketRetransmissionCount)
			{
				sendPacketInfo->owner->DoDisconnect();
				++numOfTimeoutSession;
				continue;
			}

			if (not SendPacket(sendPacketInfo, false))
			{
				sendPacketInfo->owner->DoDisconnect();
			}
		}

		SleepRemainingFrameTime(tickSet, retransmissionThreadSleepMs);
	}
}

void MultiSocketRUDPCore::RunSessionReleaseThread(const std::stop_token& stopToken)
{
	const HANDLE eventHandles[2] = { sessionReleaseEventHandle, sessionReleaseStopEventHandle };
	while (not stopToken.stop_requested())
	{
		switch (WaitForMultipleObjects(2, eventHandles, FALSE, INFINITE))
		{
		case WAIT_OBJECT_0:
		{
			std::scoped_lock lock(releaseSessionIdListLock);
			
            std::erase_if(releaseSessionIdList, [this](const SessionIdType releaseSessionId)
            {
                const auto releaseSession = GetReleasingSession(releaseSessionId);
                if (releaseSession == nullptr)
                {
                    return true;
                }

                if (releaseSession->GetSendContext().GetIOMode() == IO_MODE::IO_SENDING ||
                    releaseSession->nowInProcessingRecvPacket)
                {
                    return false;
                }

                releaseSession->Disconnect();
                releaseSession->InitializeSession();
                return true;
            });

            if (not releaseSessionIdList.empty())
            {
                SetEvent(sessionReleaseEventHandle);
            }
		}
		break;
		case WAIT_OBJECT_0 + 1:
		{
			const auto log = Logger::MakeLogObject<ServerLog>();
			log->logString = "Session release thread stop.";
			Logger::GetInstance().WriteLog(log);
			return;
		}
		default:
		{
			LOG_ERROR(std::format("RunSessionReleaseThread() : Invalid session release thread wait result. Error is {}", WSAGetLastError()));
		}
		break;
		}
	}
}

void MultiSocketRUDPCore::RunHeartbeatThread(const std::stop_token& stopToken) const
{
	TickSet tickSet;
	tickSet.nowTick = GetTickCount64();

	while (not stopToken.stop_requested())
	{
		sessionManager->HeartbeatCheck(GetTickCount64());
		SleepRemainingFrameTime(tickSet, heartbeatThreadSleepMs);
	}
}

IOContext* MultiSocketRUDPCore::GetIOCompletedContext(const RIORESULT& rioResult)
{
	const auto context = reinterpret_cast<IOContext*>(rioResult.RequestContext);
	if (context == nullptr)
	{
		return nullptr;
	}

	context->session = sessionManager->GetUsingSession(context->ownerSessionId);
	if (context->session == nullptr)
	{
		return nullptr;
	}

	if (rioResult.Status != 0)
	{
		context->session->DoDisconnect();
		LOG_ERROR(std::format("RIO operation failed with error code {}", rioResult.Status));
		if (context->ioType == RIO_OPERATION_TYPE::OP_SEND)
		{
			contextPool.Free(context);
		}
		return nullptr;
	}

	return context;
}

void MultiSocketRUDPCore::OnRecvPacket(const BYTE threadId)
{
	while (recvIOCompletedContexts[threadId].GetRestSize() > 0)
	{
		RecvIOCompletedContext* context = nullptr;
		if (recvIOCompletedContexts[threadId].Dequeue(&context) == false || context == nullptr)
		{
			continue;
		}

		NetBuffer* buffer = nullptr;
		do
		{
			if (context->session == nullptr)
			{
				break;
			}

			context->session->nowInProcessingRecvPacket = true;
			if (context->session->rioContext.GetRecvBuffer().recvBufferList.Dequeue(&buffer) == false || buffer == nullptr)
			{
				break;
			}
			packetProcessor->OnRecvPacket(*context->session
				, *buffer
				, std::span(reinterpret_cast<const unsigned char*>(context->clientAddrBuffer)
				, sizeof(context->clientAddrBuffer)));
		} while (false);

		if (buffer != nullptr)
		{
			NetBuffer::Free(buffer);
		}

		if (context->session != nullptr)
		{
			context->session->nowInProcessingRecvPacket = false;
		}
		recvIOCompletedContextPool.Free(context);
	}
}
