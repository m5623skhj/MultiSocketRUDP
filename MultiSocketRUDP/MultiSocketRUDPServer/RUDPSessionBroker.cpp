#include "PreCompile.h"
#include "RUDPSessionBroker.h"
#include "RUDPSession.h"
#include "MultiSocketRUDPCore.h"
#include "LogExtension.h"
#include "Logger.h"
#include "MultiSocketRUDPCoreFunctionDelegate.h"
#include "ISessionDelegate.h"
#include "../Common/Crypto/CryptoHelper.h"
#include "../Common/PacketCrypto/PacketCryptoHelper.h"
#include <string_view>
#include <condition_variable>
#include <ws2tcpip.h>

namespace
{
	std::uint64_t ElapsedMilliseconds(const std::chrono::steady_clock::time_point inStarted)
	{
		return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - inStarted).count());
	}

	void LogBrokerFailure(const std::uint64_t inConnectionId, const char* inStage, const char* inReason,
		const std::chrono::steady_clock::time_point inStarted,
		const char* inErrorDomain = "none", const std::int64_t inErrorCode = 0) noexcept
	{
		try
		{
			const auto log = Logger::MakeLogObject<ServerLog>();
			log->logString = std::format(
				"Broker connection failure; process={}; connection={}; stage={}; reason={}; elapsed_ms={}; error_domain={}; error_code={}; error_hex={:#010x}",
				GetCurrentProcessId(), inConnectionId, inStage, inReason, ElapsedMilliseconds(inStarted),
				inErrorDomain, inErrorCode, static_cast<std::uint32_t>(inErrorCode));
			Logger::GetInstance().WriteLog(log);
		}
		catch (...) { Logger::GetInstance().RecordDroppedDiagnostic(); }
	}

	// Bounded asynchronous diagnostics: never log while holding the broker queue lock.
	void LogBrokerProgress(const std::uint64_t inConnectionId, const char* inEvent,
		const std::uint64_t inQueueMs, const std::uint64_t inCertificateMs,
		const std::string_view inDetails = {}) noexcept
	{
		try
		{
			const auto log = Logger::MakeLogObject<ServerLog>();
			log->logString = std::format(
				"Broker TLS progress; process={}; connection={}; event={}; queue_ms={}; certificate_ms={}{}",
				GetCurrentProcessId(), inConnectionId, inEvent, inQueueMs, inCertificateMs, inDetails);
			// A scoped accept is used as evidence when the NIC trace has no matching broker record.
			// Keep it on the normal bounded queue so logger lock contention alone cannot discard it.
			if (inDetails.empty()) (void)Logger::GetInstance().TryWriteLog(log);
			else Logger::GetInstance().WriteLog(log);
		}
		catch (...)
		{
			Logger::GetInstance().RecordDroppedDiagnostic();
		}
	}

	// The external bot host opts in for a single peer. Use the tuple after NAT, as seen by the server NIC.
	void LogBrokerAccepted(const std::uint64_t inConnectionId, SOCKET inSocket,
		const sockaddr_in& inPeer, std::uint64_t inPeerAcceptNumber) noexcept
	{
		if (!inPeerAcceptNumber)
		{
			LogBrokerProgress(inConnectionId, "accepted", 0, 0);
			return;
		}
		try
		{
			char peerAddress[INET_ADDRSTRLEN]{};
			char localAddress[INET_ADDRSTRLEN]{};
			sockaddr_in local{};
			int localSize = sizeof(local);
			const bool resolved = InetNtopA(AF_INET, &inPeer.sin_addr, peerAddress, sizeof(peerAddress))
				&& getsockname(inSocket, reinterpret_cast<sockaddr*>(&local), &localSize) == 0
				&& InetNtopA(AF_INET, &local.sin_addr, localAddress, sizeof(localAddress));
			const auto endpoints = resolved
				? std::format("; peer={}:{}; local={}:{}", peerAddress, ntohs(inPeer.sin_port), localAddress, ntohs(local.sin_port))
				: std::format("; endpoint_error={}", WSAGetLastError());
			LogBrokerProgress(inConnectionId, "accepted", 0, 0,
				std::format("{}; peer_accept_number={}", endpoints, inPeerAcceptNumber));
		}
		catch (...)
		{
			Logger::GetInstance().RecordDroppedDiagnostic();
			LogBrokerProgress(inConnectionId, "accepted", 0, 0);
		}
	}

	void LogTracedQueue(std::uint64_t inConnectionId, const char* inEvent,
		std::size_t inDepth, std::chrono::steady_clock::time_point inAcceptedAt) noexcept
	{
		try
		{
			LogBrokerProgress(inConnectionId, inEvent, ElapsedMilliseconds(inAcceptedAt), 0,
				std::format("; queue_depth={}; thread={}", inDepth, GetCurrentThreadId()));
		}
		catch (...) { Logger::GetInstance().RecordDroppedDiagnostic(); }
	}

	void LogBrokerHandshake(const TLSHelper::TLSHelperServer& inTlsHelper, const bool inSucceeded,
		const std::uint64_t inConnectionId, const std::uint64_t inQueueMs,
		const std::uint64_t inCertificateMs) noexcept
	{
		try
		{
			const auto& result = inTlsHelper.GetHandshakeResult();
			auto message = std::format(
				"Broker TLS handshake {}; process={}; connection={}; queue_ms={}; certificate_ms={}; reason={}; stage={}; socket_error={}; elapsed_ms={}; buffered_bytes={}; security_status={:#010x}; tls_sent_bytes={}; tls_received_bytes={}; send_calls={}; recv_calls={}; security_calls={}",
				inSucceeded ? "succeeded" : "failed", GetCurrentProcessId(), inConnectionId, inQueueMs, inCertificateMs,
				result.reason, result.stage, result.socketError, result.elapsedMs, result.bufferedBytes,
				static_cast<unsigned long>(inTlsHelper.GetLastStatus()), result.sentBytes, result.receivedBytes,
				result.sendCalls, result.receiveCalls, result.securityCalls);
			if (inTlsHelper.HasHandshakeTrace())
				message += std::format("; hello_tag={}; first_send_utc_ms={}; first_recv_utc_ms={}",
					result.clientHelloTag ? std::format("{:016x}", *result.clientHelloTag) : "unavailable",
					result.firstSendUtcMs, result.firstReceiveUtcMs);
			if (inSucceeded)
			{
				const auto log = Logger::MakeLogObject<ServerLog>();
				log->logString = message;
				Logger::GetInstance().WriteLog(log);
			}
			else
			{
				LOG_ERROR(message);
			}
		}
		catch (...)
		{
			Logger::GetInstance().RecordDroppedDiagnostic();
		}
	}
}

RUDPSessionBroker::RUDPSessionBroker(
	MultiSocketRUDPCore& inCore,
	ISessionDelegate& inSessionDelegate,
	TLSHelper::ServerCertificateConfig inServerCertificateConfig)
	: core(inCore)
	, sessionDelegate(inSessionDelegate)
	, serverCertificateConfig(std::move(inServerCertificateConfig))
{
}

RUDPSessionBroker::~RUDPSessionBroker()
{
	Stop();
}

bool RUDPSessionBroker::Start(const PortType listenPort, const std::string& rudpSessionIP)
{
	if (isRunning)
	{
		LOG_ERROR("RUDPSessionBroker already running");
		return false;
	}

	if (not OpenSessionBrokerSocket(listenPort))
	{
		LOG_ERROR("RUDPSessionBroker OpenSessionBrokerSocket failed");
		return false;
	}

	threadPool.reserve(BROKER_THREAD_POOL_SIZE);
	for (unsigned int i = 0; i < BROKER_THREAD_POOL_SIZE; ++i)
	{
		threadPool.emplace_back([this](const std::stop_token& stopToken)
			{
				RunBrokerWorkerThread(stopToken);
			});
	}

	isRunning = true;
	sessionBrokerThread = std::jthread([this, rudpSessionIP, listenSocket = sessionBrokerListenSocket](const std::stop_token& stopToken)
		{
			this->RunSessionBrokerThread(stopToken, listenSocket, rudpSessionIP);
		});

	return true;
}

void RUDPSessionBroker::Stop()
{
	if (not isRunning)
	{
		return;
	}

	if (sessionBrokerThread.joinable())
		sessionBrokerThread.request_stop();
	CloseListenSocket();
	if (sessionBrokerThread.joinable())
	{
		sessionBrokerThread.join();
	}

	for (auto& thread : threadPool)
	{
		thread.request_stop();
	}

	{
		// Accept has stopped and workers cannot dequeue after their stop requests.
		// Close queued sockets without waiting for active workers to finish cleanup.
		std::scoped_lock lock(clientQueueLock);
		while (not clientQueue.empty())
		{
			closesocket(clientQueue.front().socket);
			clientQueue.pop();
		}
	}

	for (auto& thread : threadPool)
	{
		if (thread.joinable())
		{
			thread.join();
		}
	}
	threadPool.clear();

	isRunning = false;

	const auto log = Logger::MakeLogObject<ServerLog>();
	log->logString = "RUDPSessionBroker stopped";
	Logger::GetInstance().WriteLog(log);
}

void RUDPSessionBroker::RunSessionBrokerThread(const std::stop_token& stopToken, SOCKET listenSocket, const std::string& rudpSessionIP)
{
	IN_ADDR tracedPeer{};
	wchar_t tracedPeerText[INET_ADDRSTRLEN]{};
	const auto length = GetEnvironmentVariableW(L"ACTIONRPG_ROOM_BROKER_TRACE_PEER_IPV4", tracedPeerText, INET_ADDRSTRLEN);
	const bool traceEnabled = length > 0 && length < INET_ADDRSTRLEN && InetPtonW(AF_INET, tracedPeerText, &tracedPeer) == 1;
	// Keep health samples independent of accept and TLS work even when the last connection
	// never reaches the application. The diagnostic thread joins when the accept loop exits.
	std::jthread traceHealthThread;
	if (traceEnabled)
	{
		char peerText[INET_ADDRSTRLEN]{};
		sockaddr_in local{};
		int size = sizeof(local);
		if (InetNtopA(AF_INET, &tracedPeer, peerText, sizeof(peerText))
			&& getsockname(listenSocket, reinterpret_cast<sockaddr*>(&local), &size) == 0)
		{
			try
			{
				traceHealthThread = std::jthread([this, peer = std::string(peerText), port = ntohs(local.sin_port)](std::stop_token stop)
				{ RunTraceHealth(stop, peer, port); });
			}
			catch (...) { Logger::GetInstance().RecordDroppedDiagnostic(); }
		}
	}

	while (not stopToken.stop_requested())
	{
		sockaddr_in clientAddr{};
		int sockAddrSize = sizeof(clientAddr);
		const auto clientSocket = accept(listenSocket, reinterpret_cast<sockaddr*>(&clientAddr), &sockAddrSize);
		if (clientSocket == INVALID_SOCKET)
		{
			auto error = WSAGetLastError();
			if (error == WSAENOTSOCK)
			{
				break;
			}

			LOG_ERROR(std::format("RunSessionBrokerThread accept failed with error {}", error));
			continue;
		}

		const bool tracePeer = traceEnabled && clientAddr.sin_family == AF_INET
			&& clientAddr.sin_addr.s_addr == tracedPeer.s_addr;
		PendingConnection connection{
			clientSocket, rudpSessionIP, ++nextConnectionId, std::chrono::steady_clock::now(), tracePeer };
		const auto connectionId = connection.connectionId;
		const auto acceptedAt = connection.acceptedAt;
		const auto peerOrdinal = tracePeer ? tracedAccepts.fetch_add(1, std::memory_order_relaxed) + 1 : 0;
		LogBrokerAccepted(connectionId, clientSocket, clientAddr, peerOrdinal);
		bool queued{};
		std::size_t depth{};
		{
			std::scoped_lock lock(clientQueueLock);
			// Bound unauthenticated work; expired entries are also rejected before initialization.
			if (clientQueue.size() < MAX_QUEUED_CONNECTIONS) { clientQueue.push(std::move(connection)); queued = true; }
			depth = clientQueue.size();
		}
		if (!queued)
		{
			if (tracePeer) tracedQueueRejections.fetch_add(1, std::memory_order_relaxed);
			LogBrokerFailure(connection.connectionId, "queue", "queue-full", connection.acceptedAt);
			closesocket(clientSocket);
			continue;
		}
		clientQueueCV.notify_one();
		if (tracePeer) LogTracedQueue(connectionId, "queued", depth, acceptedAt);
	}

	const auto log = Logger::MakeLogObject<ServerLog>();
	log->logString = "Session broker thread stopped";
	Logger::GetInstance().WriteLog(log);
}

void RUDPSessionBroker::RunTraceHealth(std::stop_token inStopToken, const std::string& inPeer, PortType inPort) const noexcept
{
	try
	{
		std::mutex mutex;
		std::condition_variable_any wake;
		std::unique_lock lock(mutex);
		for (std::uint64_t sample = 0; !inStopToken.stop_requested(); ++sample)
		{
			const auto counts = Logger::GetInstance().GetStatistics();
			const auto log = Logger::MakeLogObject<ServerLog>();
			log->logString = std::format("Broker WAN trace; event={}; version=2; process={}; peer={}; listen_port={}; sample={}; "
				"workers={}; queue_capacity={}; accepted={}; dequeued={}; completed={}; queue_rejected={}; "
				"normal_dropped={}; diagnostic_dropped={}; file_errors={}; serialization_errors={}; console_errors={}; stop_timeouts={}",
				sample ? "health" : "ready", GetCurrentProcessId(), inPeer, inPort, sample, BROKER_THREAD_POOL_SIZE,
				MAX_QUEUED_CONNECTIONS, tracedAccepts.load(std::memory_order_relaxed), tracedDequeues.load(std::memory_order_relaxed),
				tracedCompletions.load(std::memory_order_relaxed), tracedQueueRejections.load(std::memory_order_relaxed),
				counts.normalDropped, counts.diagnosticDropped, counts.fileErrors, counts.serializationErrors,
				counts.consoleErrors, counts.stopTimeouts);
			Logger::GetInstance().WriteLog(log);
			wake.wait_for(lock, inStopToken, std::chrono::seconds(5), [] { return false; });
		}
	}
	catch (...) { Logger::GetInstance().RecordDroppedDiagnostic(); }
}

void RUDPSessionBroker::RunBrokerWorkerThread(const std::stop_token& stopToken)
{
	std::stop_callback stopCallback(stopToken, [this]()
		{
			std::scoped_lock lock(clientQueueLock);
			clientQueueCV.notify_all();
		});

	while (not stopToken.stop_requested())
	{
		PendingConnection connection;
		std::size_t depth{};

		{
			std::unique_lock lock(clientQueueLock);
			clientQueueCV.wait(lock, [this, &stopToken]()
				{
					return not clientQueue.empty() || stopToken.stop_requested();
				});

			if (stopToken.stop_requested())
			{
				return;
			}

			connection = std::move(clientQueue.front());
			clientQueue.pop();
			depth = clientQueue.size();
		}

		if (connection.traceEnabled)
		{
			tracedDequeues.fetch_add(1, std::memory_order_relaxed);
			LogTracedQueue(connection.connectionId, "dequeued", depth, connection.acceptedAt);
		}
		const SOCKET clientSocket = connection.socket;
		{
			// Destroy the callback before closing: shutdown must never target a reused handle.
			std::stop_callback cancelConnection(stopToken, [clientSocket]()
				{
					shutdown(clientSocket, SD_BOTH);
				});
			if (not stopToken.stop_requested())
			{
				HandleClientConnection(connection, stopToken);
			}
		}
		closesocket(clientSocket);
		if (connection.traceEnabled)
		{
			tracedCompletions.fetch_add(1, std::memory_order_relaxed);
			LogBrokerProgress(connection.connectionId, "worker_closed", 0, 0, "; wan_trace=1");
		}
	}
}

void RUDPSessionBroker::HandleClientConnection(const PendingConnection& inConnection, const std::stop_token& stopToken)
{
	const SOCKET clientSocket = inConnection.socket;
	const auto& rudpSessionIP = inConnection.rudpSessionIP;
	const auto connectionId = inConnection.connectionId;
	const auto queueMs = ElapsedMilliseconds(inConnection.acceptedAt);
    const auto deadline = inConnection.acceptedAt + std::chrono::seconds(12);
    if (std::chrono::steady_clock::now() >= deadline)
    { LogBrokerFailure(connectionId, "queue", "timeout", inConnection.acceptedAt); return; }
	const auto traceDetails = inConnection.traceEnabled ? "; wan_trace=1" : "";
	LogBrokerProgress(connectionId, "worker_started", queueMs, 0, traceDetails);
	// Bound each blocking socket operation, including the initial TLS receive.
	constexpr DWORD BROKER_IO_TIMEOUT_MS = 12000;
	if (setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO,
		reinterpret_cast<const char*>(&BROKER_IO_TIMEOUT_MS), sizeof(BROKER_IO_TIMEOUT_MS)) == SOCKET_ERROR ||
		setsockopt(clientSocket, SOL_SOCKET, SO_SNDTIMEO,
			reinterpret_cast<const char*>(&BROKER_IO_TIMEOUT_MS), sizeof(BROKER_IO_TIMEOUT_MS)) == SOCKET_ERROR)
	{
		const int socketError = WSAGetLastError();
		LOG_ERROR(std::format("Broker socket timeout setup failed with error {}; process={}; connection={}; queue_ms={}", socketError, GetCurrentProcessId(), connectionId, queueMs));
		return;
	}

	TLSHelper::TLSHelperServer localTlsHelper(serverCertificateConfig);
	if (inConnection.traceEnabled) localTlsHelper.EnableHandshakeTrace();
    localTlsHelper.SetConnectionDeadline(deadline);

	const auto certificateStarted = std::chrono::steady_clock::now();
	const bool initialized = localTlsHelper.Initialize();
	const auto certificateMs = ElapsedMilliseconds(certificateStarted);
	if (not initialized)
	{
		LOG_ERROR(std::format(
			"HandleClientConnection localTlsHelper.Initialize() failed with security status {:#010x}; process={}; connection={}; queue_ms={}; certificate_ms={}",
			static_cast<unsigned long>(localTlsHelper.GetLastStatus()), GetCurrentProcessId(), connectionId, queueMs, certificateMs));
		return;
	}
	LogBrokerProgress(connectionId, "certificate_ready", queueMs, certificateMs, traceDetails);

	const bool handshakeSucceeded = localTlsHelper.Handshake(clientSocket, stopToken);
	LogBrokerHandshake(localTlsHelper, handshakeSucceeded, connectionId, queueMs, certificateMs);
	if (not handshakeSucceeded)
	{
		return;
	}
	if (not localTlsHelper.ReceiveProtocolVersion(clientSocket, RUDP_PROTOCOL_VERSION, stopToken))
	{
		const auto& result = localTlsHelper.GetProtocolVersionResult();
		LOG_ERROR(std::format("RUDP protocol negotiation failed; reason={}; expected_version={}; received_version={}; plain_bytes={}; socket_error={}; security_status={:#010x}; process={}; connection={}",
			result.reason, RUDP_PROTOCOL_VERSION,
			result.receivedVersion ? std::to_string(*result.receivedVersion) : "unavailable",
			result.plainBytes, result.socketError, static_cast<unsigned long>(localTlsHelper.GetLastStatus()), GetCurrentProcessId(), connectionId));
		return;
	}
	if (inConnection.traceEnabled) LogBrokerProgress(connectionId, "protocol_received", queueMs, certificateMs, traceDetails);
	NetBuffer sendBuffer;
	sendBuffer.Init();

	if (!localTlsHelper.ApplyRemainingTimeout(clientSocket))
	{
		const int socketError = WSAGetLastError();
		LogBrokerFailure(connectionId, "session-response-setup", socketError == WSAETIMEDOUT
			? "timeout" : "socket-timeout-setup-failed", inConnection.acceptedAt, "winsock", socketError);
		return;
	}
	const auto session = ReserveSession(sendBuffer, rudpSessionIP, connectionId, inConnection.traceEnabled);
	if (not SendSessionInfoToClient(clientSocket, localTlsHelper, sendBuffer, connectionId))
	{
		if (session != nullptr)
		{
			sessionDelegate.AbortReservedSession(*session);
		}
	}
	else if (inConnection.traceEnabled)
	{
		LogBrokerProgress(connectionId, "session_response_sent", queueMs, certificateMs, traceDetails);
	}
}

bool RUDPSessionBroker::OpenSessionBrokerSocket(const PortType listenPort)
{
	sessionBrokerListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sessionBrokerListenSocket == INVALID_SOCKET)
	{
		LOG_ERROR(std::format("RunSessionBrokerThread socket creation failed with error {}", WSAGetLastError()));
		return false;
	}

	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.S_un.S_addr = INADDR_ANY;
	serverAddr.sin_port = htons(listenPort);

	if (bind(sessionBrokerListenSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR)
	{
		LOG_ERROR(std::format("RunSessionBrokerThread bind failed with error {}", WSAGetLastError()));
		closesocket(sessionBrokerListenSocket);
		return false;
	}

	if (listen(sessionBrokerListenSocket, SOMAXCONN) == SOCKET_ERROR)
	{
		LOG_ERROR(std::format("RunSessionBrokerThread listen failed with error {}", WSAGetLastError()));
		closesocket(sessionBrokerListenSocket);
		return false;
	}

	return true;
}

void RUDPSessionBroker::CloseListenSocket()
{
	if (sessionBrokerListenSocket != INVALID_SOCKET)
	{
		closesocket(sessionBrokerListenSocket);
		sessionBrokerListenSocket = INVALID_SOCKET;
	}
}

RUDPSession* RUDPSessionBroker::ReserveSession(OUT NetBuffer& sendBuffer, const std::string& rudpServerIP,
	const std::uint64_t inConnectionId, const bool inTraceEnabled) const
{
	const auto started = std::chrono::steady_clock::now();
	CONNECT_RESULT_CODE connectResultCode;
	const auto session = MultiSocketRUDPCoreFunctionDelegate::AcquireSession();
	do
	{
		if (session == nullptr)
		{
			connectResultCode = CONNECT_RESULT_CODE::SERVER_FULL;
			break;
		}

		connectResultCode = InitReserveSession(*session);
	} while (false);

	if (connectResultCode == CONNECT_RESULT_CODE::SUCCESS)
	{
		if (not InitSessionCrypto(*session))
		{
			connectResultCode = CONNECT_RESULT_CODE::SESSION_KEY_GENERATION_FAILED;
		}
	}

	if (connectResultCode != CONNECT_RESULT_CODE::SUCCESS)
		LogBrokerFailure(inConnectionId, "session-reserve", "reservation-failed", started,
			"broker-result", static_cast<int>(connectResultCode));
	sendBuffer.Init();
	sendBuffer << RUDP_PROTOCOL_VERSION << connectResultCode;
	if (connectResultCode == CONNECT_RESULT_CODE::SUCCESS && session != nullptr)
	{
		SetSessionInfoToBuffer(*session, rudpServerIP, sendBuffer);
		sessionDelegate.SetSessionReservedTime(*session, GetTickCount64());
		if (inTraceEnabled)
		{
			try
			{
				PortType port{};
				SessionIdType sessionId{};
				sessionDelegate.GetServerPortAndSessionId(*session, port, sessionId);
				LogBrokerProgress(inConnectionId, "rudp_reserved", 0, 0,
					std::format("; udp_port={}; rudp_session={}", port, sessionId));
			}
			catch (...) { Logger::GetInstance().RecordDroppedDiagnostic(); }
		}
	}
	else
	{
		if (session != nullptr)
		{
			if (session->IsReserved())
			{
				sessionDelegate.AbortReservedSession(*session);
			}
			else
			{
				session->DoDisconnect(DISCONNECT_REASON::BY_ERROR);
			}
		}
	}

	return session;
}

CONNECT_RESULT_CODE RUDPSessionBroker::InitReserveSession(OUT RUDPSession& session)
{
	if (session.IsConnected())
	{
		LOG_ERROR("This session already connected");
		return CONNECT_RESULT_CODE::ALREADY_CONNECTED_SESSION;
	}

	return MultiSocketRUDPCoreFunctionDelegate::InitReserveSession(session);
}

bool RUDPSessionBroker::InitSessionCrypto(OUT RUDPSession& session) const
{
	if (not GenerateSessionKey(session) || not GenerateSaltKey(session))
	{
		return false;
	}

	if (sessionDelegate.GetSessionKeyHandle(session) == nullptr)
	{
		sessionDelegate.SetSessionKeyObjectBuffer(session, new unsigned char[CryptoHelper::GetTLSInstance().GetKeyObjectSize()]);
	}

	sessionDelegate.SetSessionKeyHandle(session, 
		CryptoHelper::GetTLSInstance().GetSymmetricKeyHandle(
			sessionDelegate.GetSessionKeyObjectBuffer(session),
			const_cast<unsigned char*>(sessionDelegate.GetSessionKey(session))));
	if (sessionDelegate.GetSessionKeyHandle(session) == nullptr)
	{
		LOG_ERROR("InitSessionCrypto failed : GetSymmetricKeyHandle failed");
		return false;
	}

	return true;
}

bool RUDPSessionBroker::GenerateSessionKey(OUT RUDPSession& session) const
{
	if (const auto bytes = CryptoHelper::GenerateSecureRandomBytes(SESSION_KEY_SIZE); bytes.has_value())
	{
		sessionDelegate.SetSessionKey(session, bytes->data());
		return true;
	}

	return false;
}

bool RUDPSessionBroker::GenerateSaltKey(OUT RUDPSession& session) const
{
	if (const auto bytes = CryptoHelper::GenerateSecureRandomBytes(SESSION_SALT_SIZE); bytes.has_value())
	{
		sessionDelegate.SetSessionSalt(session, bytes->data());
		return true;
	}

	return false;
}

void RUDPSessionBroker::SetSessionInfoToBuffer(const RUDPSession& session, const std::string& rudpServerIP, OUT NetBuffer& buffer) const
{
	PortType targetPort;
	SessionIdType sessionId;
	sessionDelegate.GetServerPortAndSessionId(session, targetPort, sessionId);

	//Send rudp session information packet to client
	buffer << rudpServerIP << targetPort << sessionId;
	buffer.WriteBuffer(sessionDelegate.GetSessionKey(session), SESSION_KEY_SIZE);
	buffer.WriteBuffer(sessionDelegate.GetSessionSalt(session), SESSION_SALT_SIZE);
}

bool RUDPSessionBroker::SendSessionInfoToClient(const SOCKET& clientSocket, TLSHelper::TLSHelperServer& localTlsHelper,
	OUT NetBuffer& sendBuffer, const std::uint64_t inConnectionId)
{
	const auto started = std::chrono::steady_clock::now();
	PacketCryptoHelper::SetHeader(sendBuffer);

	constexpr size_t maxTlsPacketSize = 16 * 1024 + 512;
	char encryptedBuffer[maxTlsPacketSize];
	size_t encryptedSize = 0;

	if (not localTlsHelper.EncryptData(
		sendBuffer.GetBufferPtr(),
		sendBuffer.GetAllUseSize(),
		encryptedBuffer,
		encryptedSize))
	{
		const auto status = localTlsHelper.GetLastStatus();
		LogBrokerFailure(inConnectionId, "session-encrypt", "encrypt-failed", started, "sspi", status);
		return false;
	}

	if (not SendAll(clientSocket, encryptedBuffer, encryptedSize, localTlsHelper, inConnectionId, "session-send"))
	{
		return false;
	}

	if (localTlsHelper.EncryptCloseNotify(encryptedBuffer, sizeof(encryptedBuffer), encryptedSize))
	{
		if (not SendAll(clientSocket, encryptedBuffer, encryptedSize, localTlsHelper, inConnectionId, "close-notify-send"))
		{
			return false;
		}
	}
	else
	{
		// Existing behavior tolerates close_notify encryption failure after sending the payload.
		const auto status = localTlsHelper.GetLastStatus();
		LogBrokerFailure(inConnectionId, "close-notify-encrypt", "encrypt-failed", started, "sspi", status);
	}

	// Application data and close_notify are already sent. Do not extend the budget waiting for peer FIN.
	shutdown(clientSocket, SD_SEND);

	sendBuffer.Init();

	return true;
}

bool RUDPSessionBroker::SendAll(const SOCKET& socket, const char* sendBuffer, const size_t sendSize,
	TLSHelper::TLSHelperServer& tls, const std::uint64_t inConnectionId, const char* inStage)
{
	const auto started = std::chrono::steady_clock::now();
	size_t sent = 0;
	while (sent < sendSize)
	{
		if (!tls.ApplyRemainingTimeout(socket))
		{
			const int socketError = WSAGetLastError();
			LogBrokerFailure(inConnectionId, inStage, socketError == WSAETIMEDOUT
				? "timeout" : "socket-timeout-setup-failed", started, "winsock", socketError);
			return false;
		}
		const int ret = send(socket, sendBuffer + sent, static_cast<int>(sendSize - sent), 0);
		if (ret <= 0)
		{
			const int socketError = ret == SOCKET_ERROR ? WSAGetLastError() : 0;
			LogBrokerFailure(inConnectionId, inStage, ret == 0 ? "peer-closed"
				: socketError == WSAETIMEDOUT ? "timeout" : "socket-send-failed", started,
				socketError ? "winsock" : "none", socketError);
			return false;
		}

		sent += ret;
	}

	return true;
}
