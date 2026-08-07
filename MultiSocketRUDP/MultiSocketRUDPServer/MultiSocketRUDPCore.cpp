#include "PreCompile.h"
#include "MultiSocketRUDPCore.h"
#include "BuildConfig.h"
#include "LogExtension.h"
#include "Logger.h"
#include "Ticker.h"
#include "MultiSocketRUDPCoreFunctionDelegate.h"
#include "RIOManager.h"
#include "RUDPSessionBroker.h"
#include "SendPacketInfo.h"
#include "../Common/etc/UtilFunc.h"
#include "RUDPSessionManager.h"
#include "RUDPThreadManager.h"
#include "RUDPPacketProcessor.h"
#include "RUDPIOHandler.h"

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace
{
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

		return sock;
	}

	// ----------------------------------------
	// @brief Closes a valid Windows handle and resets it to NULL to prevent double close.
	// ----------------------------------------
	void CloseHandleIfValid(HANDLE& handle)
	{
		if (handle == NULL)
		{
			return;
		}

		CloseHandle(handle);
		handle = NULL;
	}
}

void MultiSocketRUDPCore::RecvIOCompletedQueue::Enqueue(RecvIOCompletedContext* const inContext)
{
	std::scoped_lock lock(queueLock);
	contexts.push(inContext);
}

bool MultiSocketRUDPCore::RecvIOCompletedQueue::Dequeue(RecvIOCompletedContext** const outContext)
{
	if (outContext == nullptr)
	{
		return false;
	}

	std::scoped_lock lock(queueLock);
	if (contexts.empty())
	{
		return false;
	}

	*outContext = contexts.front();
	contexts.pop();
	return true;
}

MultiSocketRUDPCore::MultiSocketRUDPCore(std::wstring&& inSessionBrokerCertStoreName
	, std::wstring&& inSessionBrokerCertSubjectName)
	: MultiSocketRUDPCore(TLSHelper::ServerCertificateConfig::FromStore(inSessionBrokerCertStoreName, inSessionBrokerCertSubjectName))
{
}

MultiSocketRUDPCore::MultiSocketRUDPCore(TLSHelper::ServerCertificateConfig inSessionBrokerCertificateConfig)
	: sessionBrokerCertificateConfig(std::move(inSessionBrokerCertificateConfig))
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
		StopLoggerThread();
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
		StopServer();
		return false;
	}

	if (not sessionManager->Initialize(numOfWorkerThread, std::move(factoryFunc)))
	{
		LOG_ERROR("Session factory function is not set");
		StopServer();
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
	if (sessionBroker != nullptr)
	{
		sessionBroker->Stop();
	}

	CloseAllSessions();
	WaitForAllSessionsReleased();
	SignalWorkerStopEvents();
	StopAllThreads();
	CloseWorkerEventHandles();
	CloseRetransmissionSchedulerHandles();
	retransmissionSchedulers.clear();

	Ticker::GetInstance().Stop();

	ClearAllSession();
	MultiSocketRUDPCoreFunctionDelegate::Instance().Clear(*this);
	StopLoggerThread();

	WSACleanup();
	isServerStopped = true;
}

void MultiSocketRUDPCore::SetFatalErrorHandler(ServerFatalErrorHandler inHandler)
{
	ServerFatalErrorHandler handlerToNotify;
	std::optional<ServerFatalError> errorToNotify;
	{
		std::scoped_lock lock(fatalErrorLock);
		fatalErrorHandler = std::move(inHandler);
		if (fatalErrorHandler != nullptr && fatalError.has_value())
		{
			handlerToNotify = fatalErrorHandler;
			errorToNotify = fatalError;
		}
	}

	if (handlerToNotify != nullptr && errorToNotify.has_value())
	{
		NotifyFatalErrorHandler(handlerToNotify, errorToNotify.value());
	}
}

std::optional<ServerFatalError> MultiSocketRUDPCore::GetFatalError() const
{
	std::scoped_lock lock(fatalErrorLock);
	return fatalError;
}

void MultiSocketRUDPCore::ReportFatalError(const ServerFatalError& error)
{
	ServerFatalErrorHandler handlerToNotify;
	{
		std::scoped_lock lock(fatalErrorLock);
		if (fatalError.has_value())
		{
			return;
		}

		fatalError = error;
		handlerToNotify = fatalErrorHandler;
	}

	if (handlerToNotify == nullptr)
	{
		LOG_ERROR("Fatal server error occurred without a registered upper-layer handler");
		return;
	}

	NotifyFatalErrorHandler(handlerToNotify, error);
}

void MultiSocketRUDPCore::NotifyFatalErrorHandler(
	const ServerFatalErrorHandler& handler,
	const ServerFatalError& error) noexcept
{
	try
	{
		handler(error);
	}
	catch (const std::exception& exception)
	{
		LOG_ERROR(std::format("Fatal server error handler threw an exception: {}", exception.what()));
	}
	catch (...)
	{
		LOG_ERROR("Fatal server error handler threw an unknown exception");
	}
}

bool MultiSocketRUDPCore::IsServerStopped() const
{
	return isServerStopped;
}

unsigned short MultiSocketRUDPCore::GetNowSessionCount() const
{
	return sessionManager->GetNowSessionCount();
}

unsigned short MultiSocketRUDPCore::GetUnusedSessionCount() const
{
	return sessionManager->GetUnusedSessionCount();
}

unsigned int MultiSocketRUDPCore::GetAllConnectedCount() const
{
	return sessionManager->GetAllConnectedCount();
}

unsigned int MultiSocketRUDPCore::GetAllDisconnectedCount() const
{
	return sessionManager->GetAllDisconnectedCount();
}

unsigned int MultiSocketRUDPCore::GetAllDisconnectedByRetransmissionCount() const
{
	return sessionManager->GetAllDisconnectedByRetransmissionCount();
}

bool MultiSocketRUDPCore::SendPacket(SendPacketInfo* sendPacketInfo) const
{
	if (sendPacketInfo == nullptr || sendPacketInfo->owner == nullptr || sendPacketInfo->GetBuffer() == nullptr)
	{
		LOG_ERROR("SendPacketInfo or its owner or its buffer is nullptr in MultiSocketRUDPCore::SendPacket()");
		return false;
	}

	if (not sendPacketInfo->IsOwnerValid())
	{
		LOG_ERROR("SendPacketInfo owner is invalid with generation mismatch in MultiSocketRUDPCore::SendPacket()");
		return false;
	}

	if (sendPacketInfo->owner->nowInReleaseThread.load(std::memory_order_acquire))
	{
		return false;
	}

	if (sendPacketInfo->retransmissionCount == 0)
	{
		NetBuffer* buffer = sendPacketInfo->GetBuffer();
		buffer->m_iWriteLast = buffer->m_iWrite;
		buffer->m_iWrite = 0;
		buffer->m_iRead = 0;
	}

	sendPacketInfo->AddRefCount();
	sendPacketInfo->owner->rioContext.GetSendContext().PushSendPacketInfo(sendPacketInfo);
	if (not ioHandler->DoSend(*sendPacketInfo->owner, sendPacketInfo->owner->threadId))
	{
		return false;
	}

	return true;
}

void MultiSocketRUDPCore::MarkSendPacketInfoErased(OUT SendPacketInfo* eraseTarget, const ThreadIdType threadId)
{
	if (eraseTarget == nullptr || eraseTarget->isErasedPacketInfo.load(std::memory_order_acquire))
	{
		return;
	}

	if (threadId >= retransmissionSchedulers.size() || retransmissionSchedulers[threadId] == nullptr)
	{
		eraseTarget->isErasedPacketInfo.store(true, std::memory_order_release);
		return;
	}

	{
		std::scoped_lock lock(retransmissionSchedulers[threadId]->lock);
		if (eraseTarget->isErasedPacketInfo.load(std::memory_order_acquire))
		{
			return;
		}

		eraseTarget->isErasedPacketInfo.store(true, std::memory_order_release);
	}
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

unsigned int MultiSocketRUDPCore::GetHeartbeatThreadSleepMs() const
{
	return heartbeatThreadSleepMs;
}

unsigned int MultiSocketRUDPCore::GetInitialRetransmissionMs() const
{
	return retransmissionMs;
}

unsigned int MultiSocketRUDPCore::GetMinRetransmissionMs() const
{
	return minRetransmissionMs;
}

unsigned int MultiSocketRUDPCore::GetMaxRetransmissionMs() const
{
	return maxRetransmissionMs;
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
	session.nowInReleaseThread.store(true, std::memory_order_seq_cst);
	session.onSessionReleaseTime = GetTickCount64();
	releaseSessionIdList.emplace_back(session.GetSessionId());
	SetEvent(sessionReleaseEventHandle);
}

void MultiSocketRUDPCore::StopLoggerThread()
{
	const auto log = Logger::MakeLogObject<ServerLog>();
	log->logString = "Server stop";
	Logger::GetInstance().WriteLog(log);
	Logger::GetInstance().StopLoggerThread();
}

bool MultiSocketRUDPCore::EnqueueContextResult(const IOContext* contextResult, NetBuffer* buffer, const BYTE threadId)
{
	if (contextResult == nullptr || contextResult->session == nullptr ||
		contextResult->ownerRecvBuffer == nullptr || buffer == nullptr)
	{
		LOG_ERROR("Invalid context or buffer in EnqueueContextResult()");
		return false;
	}

	const auto recvIOContext = recvIOCompletedContextPool.Alloc();
	if (recvIOContext == nullptr)
	{
		LOG_ERROR("recvIOContext is nullptr in EnqueueContextResult()");
		return false;
	}

	contextResult->ownerRecvBuffer->BeginRecvLogic();
	recvIOContext->InitContext(contextResult->session,
		contextResult->ownerRecvBuffer,
		contextResult->ownerSessionGeneration,
		buffer,
		contextResult->clientAddrBuffer);
	recvIOCompletedContexts[threadId]->Enqueue(recvIOContext);
	SignalRecvLogicThread(threadId);

	return true;
}

void MultiSocketRUDPCore::SignalRecvLogicThread(const BYTE threadId)
{
	if (SetEvent(recvLogicThreadEventHandles[threadId]))
	{
		return;
	}

	const unsigned long errorCode = GetLastError();
	LOG_ERROR(std::format("SetEvent failed in EnqueueContextResult() with error {}", errorCode));
	ReportFatalError({ SERVER_FATAL_ERROR_CODE::RECV_LOGIC_EVENT_SIGNAL_FAILED, threadId, errorCode });
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
		ioHandler = std::make_unique<RUDPIOHandler>(*rioManager, sessionDelegate, contextPool, retransmissionSchedulers, maxHoldingPacketQueueSize, retransmissionMs,
			simulatedPacketLossPercent, simulatedPacketLossSeed);
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

	if (not CreateWorkerEventHandles() || not InitializeWorkerResources())
	{
		return false;
	}

	StartWorkerThreads();
	return StartSessionBroker();
}

bool MultiSocketRUDPCore::CreateWorkerEventHandles()
{
	retransmissionSchedulers.reserve(numOfWorkerThread);

	recvLogicThreadEventStopHandle = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	sessionReleaseStopEventHandle = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	sessionReleaseEventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	retransmissionStopEventHandle = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	if (recvLogicThreadEventStopHandle == NULL
		|| sessionReleaseStopEventHandle == NULL
		|| sessionReleaseEventHandle == NULL
		|| retransmissionStopEventHandle == NULL)
	{
		LOG_ERROR(std::format("Thread event handle creation failed. error is {}", GetLastError()));
		return false;
	}

	return true;
}

bool MultiSocketRUDPCore::InitializeWorkerResources()
{
	recvIOCompletedContexts.reserve(numOfWorkerThread);

	Ticker::GetInstance().Start(timerTickMs);
	for (unsigned char id = 0; id < numOfWorkerThread; ++id)
	{
		recvIOCompletedContexts.emplace_back(std::make_unique<RecvIOCompletedQueue>());

		const HANDLE recvLogicEventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (recvLogicEventHandle == NULL)
		{
			LOG_ERROR(std::format("Recv logic event handle creation failed. error is {}", GetLastError()));
			return false;
		}
		recvLogicThreadEventHandles.emplace_back(recvLogicEventHandle);

		auto scheduler = CreateRetransmissionScheduler();
		if (scheduler == nullptr)
		{
			return false;
		}

		retransmissionSchedulers.push_back(std::move(scheduler));
	}

	return true;
}

std::unique_ptr<RetransmissionScheduler> MultiSocketRUDPCore::CreateRetransmissionScheduler() const
{
	auto scheduler = std::make_unique<RetransmissionScheduler>();
	scheduler->timerHandle = CreateWaitableTimerExW(
		nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	if (scheduler->timerHandle == nullptr)
	{
		scheduler->timerHandle = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
	}

	scheduler->wakeEventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (scheduler->timerHandle != nullptr && scheduler->wakeEventHandle != nullptr)
	{
		return scheduler;
	}

	LOG_ERROR("Retransmission scheduler handle creation failed");
	CloseHandleIfValid(scheduler->timerHandle);
	CloseHandleIfValid(scheduler->wakeEventHandle);
	return nullptr;
}

void MultiSocketRUDPCore::StartWorkerThreads()
{
	threadManager->StartThreads(THREAD_GROUP::SESSION_RELEASE_THREAD, [this](const std::stop_token& stopToken, unsigned char _) { this->RunSessionReleaseThread(stopToken); }, 1);
	threadManager->StartThreads(THREAD_GROUP::HEARTBEAT_THREAD, [this](const std::stop_token& stopToken, unsigned char _) { this->RunHeartbeatThread(stopToken); }, 1);

	threadManager->StartThreads(THREAD_GROUP::IO_WORKER_THREAD, [this](const std::stop_token& stopToken, const unsigned char id) { this->RunIOWorkerThread(stopToken, id); }, numOfWorkerThread);
	threadManager->StartThreads(THREAD_GROUP::RECV_LOGIC_WORKER_THREAD, [this](const std::stop_token& stopToken, const unsigned char id) { this->RunRecvLogicWorkerThread(stopToken, id); }, numOfWorkerThread);
	threadManager->StartThreads(THREAD_GROUP::RETRANSMISSION_THREAD, [this](const std::stop_token& stopToken, const unsigned char id) { this->RunRetransmissionThread(stopToken, id); }, numOfWorkerThread);
}

bool MultiSocketRUDPCore::StartSessionBroker()
{
	Sleep(1000);
	sessionBroker = std::make_unique<RUDPSessionBroker>(*this, sessionDelegate, sessionBrokerCertificateConfig);
	if (not sessionBroker->Start(sessionBrokerPort, coreServerIp))
	{
		LOG_ERROR("RunSessionBroker failed");
		return false;
	}

	return true;
}

void MultiSocketRUDPCore::CloseAllSessions() const
{
	if (sessionManager == nullptr)
	{
		return;
	}

	sessionManager->CloseAllSessions();
}

void MultiSocketRUDPCore::WaitForAllSessionsReleased() const
{
	if (sessionManager == nullptr || not sessionManager->IsInitialized())
	{
		return;
	}

	unsigned long long lastWarningTime = GetTickCount64();
	while (sessionManager->GetUnusedSessionCount() < sessionManager->GetMaxSessions())
	{
		Sleep(1);
		const auto now = GetTickCount64();
		if (now - lastWarningTime < 10000)
		{
			continue;
		}

		LOG_ERROR(std::format(
			"StopServer is waiting for session I/O drain. released={}/{}",
			sessionManager->GetUnusedSessionCount(),
			sessionManager->GetMaxSessions()));
		lastWarningTime = now;
	}
}

void MultiSocketRUDPCore::SignalWorkerStopEvents() const
{
	if (recvLogicThreadEventStopHandle != NULL)
	{
		SetEvent(recvLogicThreadEventStopHandle);
	}

	if (sessionReleaseStopEventHandle != NULL)
	{
		SetEvent(sessionReleaseStopEventHandle);
	}

	if (retransmissionStopEventHandle != NULL)
	{
		SetEvent(retransmissionStopEventHandle);
	}
}

void MultiSocketRUDPCore::CloseWorkerEventHandles()
{
	for (auto& recvLogicThreadEventHandle : recvLogicThreadEventHandles)
	{
		CloseHandleIfValid(recvLogicThreadEventHandle);
	}
	recvLogicThreadEventHandles.clear();

	CloseHandleIfValid(recvLogicThreadEventStopHandle);
	CloseHandleIfValid(sessionReleaseEventHandle);
	CloseHandleIfValid(sessionReleaseStopEventHandle);
	CloseHandleIfValid(retransmissionStopEventHandle);
}

void MultiSocketRUDPCore::CloseRetransmissionSchedulerHandles()
{
	for (const auto& scheduler : retransmissionSchedulers)
	{
		if (scheduler == nullptr)
		{
			continue;
		}

		CloseHandleIfValid(scheduler->timerHandle);
		CloseHandleIfValid(scheduler->wakeEventHandle);
	}
}

void MultiSocketRUDPCore::ClearAllSession()
{
	{
		std::scoped_lock lock(releaseSessionIdListLock);
		releaseSessionIdList.clear();
	}

	if (sessionManager == nullptr)
	{
		return;
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
	session.sessionReservedTime = GetTickCount64();
	session.stateMachine.SetReserved();
	auto releaseOnFailure = Util::MakeScopeExit([&session]() {
		session.AbortReservedSession();
	});

	session.socketContext.SetSocket(CreateRUDPSocket());
	const SOCKET sock = session.GetSocket();
	if (sock == INVALID_SOCKET)
	{
		LOG_ERROR(std::format("CreateRUDPSocket failed with error {}", WSAGetLastError()));
		return CONNECT_RESULT_CODE::CREATE_SOCKET_FAILED;
	}

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

	releaseOnFailure.Dismiss();
	return CONNECT_RESULT_CODE::SUCCESS;
}

void MultiSocketRUDPCore::StopAllThreads() const
{
	if (threadManager == nullptr)
	{
		return;
	}

	threadManager->StopAllThreads();
}

void MultiSocketRUDPCore::OnRecvPacket(const BYTE threadId)
{
	while (true)
	{
		RecvIOCompletedContext* context = nullptr;
		if (not recvIOCompletedContexts[threadId]->Dequeue(&context))
		{
			break;
		}

		if (context == nullptr)
		{
			continue;
		}

		ProcessRecvIOCompletedContext(context);
	}
}

void MultiSocketRUDPCore::ProcessRecvIOCompletedContext(RecvIOCompletedContext* const context)
{
	const bool processingStarted = TryDispatchRecvPacket(context);
	CompleteRecvIOCompletedContext(context, processingStarted);
}

bool MultiSocketRUDPCore::TryDispatchRecvPacket(RecvIOCompletedContext* const context)
{
	if (context->session == nullptr || context->buffer == nullptr || context->ownerRecvBuffer == nullptr)
	{
		return false;
	}

	if (context->session->GetSessionGeneration() != context->ownerSessionGeneration || context->session->IsReleasing())
	{
		return false;
	}

	context->session->nowInProcessingRecvPacket.store(true, std::memory_order_release);
	packetProcessor->OnRecvPacket(*context->session
		, *context->buffer
		, std::span(reinterpret_cast<const unsigned char*>(context->clientAddrBuffer)
		, sizeof(context->clientAddrBuffer)));
	return true;
}

void MultiSocketRUDPCore::CompleteRecvIOCompletedContext(
	RecvIOCompletedContext* const context,
	const bool processingStarted)
{
	if (context->buffer != nullptr)
	{
		NetBuffer::Free(context->buffer);
	}

	if (processingStarted)
	{
		context->session->nowInProcessingRecvPacket.store(false, std::memory_order_release);
	}

	if (context->ownerRecvBuffer != nullptr)
	{
		context->ownerRecvBuffer->CompleteRecvLogic();
	}
	recvIOCompletedContextPool.Free(context);
}


