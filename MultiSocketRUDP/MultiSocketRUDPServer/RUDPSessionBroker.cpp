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

namespace
{
	std::uint64_t ElapsedMilliseconds(const std::chrono::steady_clock::time_point inStarted)
	{
		return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - inStarted).count());
	}

	// Queue diagnostics through this client checkout's Logger, outside the broker queue lock.
	void LogBrokerProgress(const std::uint64_t inConnectionId, const char* inEvent,
		const std::uint64_t inQueueMs, const std::uint64_t inCertificateMs) noexcept
	{
		try
		{
			const auto log = Logger::MakeLogObject<ServerLog>();
			log->logString = std::format(
				"Broker TLS progress; process={}; connection={}; event={}; queue_ms={}; certificate_ms={}",
				GetCurrentProcessId(), inConnectionId, inEvent, inQueueMs, inCertificateMs);
			Logger::GetInstance().WriteLog(log);
		}
		catch (...)
		{
			// Diagnostics are best-effort; do not retry the logger or fail the TLS operation.
		}
	}

	void LogBrokerHandshake(const TLSHelper::TLSHelperServer& inTlsHelper, const bool inSucceeded,
		const std::uint64_t inConnectionId, const std::uint64_t inQueueMs,
		const std::uint64_t inCertificateMs) noexcept
	{
		try
		{
			const auto& result = inTlsHelper.GetHandshakeResult();
			const auto message = std::format(
				"Broker TLS handshake {}; process={}; connection={}; queue_ms={}; certificate_ms={}; reason={}; stage={}; socket_error={}; elapsed_ms={}; buffered_bytes={}; security_status={:#010x}; tls_sent_bytes={}; tls_received_bytes={}; send_calls={}; recv_calls={}; security_calls={}",
				inSucceeded ? "succeeded" : "failed", GetCurrentProcessId(), inConnectionId, inQueueMs, inCertificateMs,
				result.reason, result.stage, result.socketError, result.elapsedMs, result.bufferedBytes,
				static_cast<unsigned long>(inTlsHelper.GetLastStatus()), result.sentBytes, result.receivedBytes,
				result.sendCalls, result.receiveCalls, result.securityCalls);
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
			// Diagnostics are best-effort; do not retry the logger or fail the TLS operation.
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
	sessionBrokerThread = std::jthread([this, rudpSessionIP](const std::stop_token& stopToken)
		{
			this->RunSessionBrokerThread(stopToken, rudpSessionIP);
		});

	return true;
}

void RUDPSessionBroker::Stop()
{
	if (not isRunning)
	{
		return;
	}

	CloseListenSocket();

	if (sessionBrokerThread.joinable())
	{
		sessionBrokerThread.request_stop();
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

void RUDPSessionBroker::RunSessionBrokerThread(const std::stop_token& stopToken, const std::string& rudpSessionIP)
{
	SOCKET clientSocket = INVALID_SOCKET;
	sockaddr_in clientAddr;
	int sockAddrSize = sizeof(clientAddr);

	while (not stopToken.stop_requested())
	{
		clientSocket = accept(sessionBrokerListenSocket, reinterpret_cast<sockaddr*>(&clientAddr), &sockAddrSize);
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

		PendingConnection connection{
			clientSocket, rudpSessionIP, ++nextConnectionId, std::chrono::steady_clock::now() };
		LogBrokerProgress(connection.connectionId, "accepted", 0, 0);
		{
			std::scoped_lock lock(clientQueueLock);
			// Bound unauthenticated work; expired entries are also rejected before initialization.
            if (clientQueue.size() >= 32) { closesocket(clientSocket); continue; }
			clientQueue.push(std::move(connection));
		}
		clientQueueCV.notify_one();
	}

	const auto log = Logger::MakeLogObject<ServerLog>();
	log->logString = "Session broker thread stopped";
	Logger::GetInstance().WriteLog(log);
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
    { LogBrokerProgress(connectionId, "queue_expired", queueMs, 0); return; }
	LogBrokerProgress(connectionId, "worker_started", queueMs, 0);
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
	LogBrokerProgress(connectionId, "certificate_ready", queueMs, certificateMs);

	const bool handshakeSucceeded = localTlsHelper.Handshake(clientSocket, stopToken);
	LogBrokerHandshake(localTlsHelper, handshakeSucceeded, connectionId, queueMs, certificateMs);
	if (not handshakeSucceeded)
	{
		return;
	}
	if (not localTlsHelper.ReceiveProtocolVersion(clientSocket, RUDP_PROTOCOL_VERSION, stopToken))
	{
		const auto& result = localTlsHelper.GetProtocolVersionResult();
		LOG_ERROR(std::format("RUDP protocol negotiation failed; reason={}; expected_version={}; received_version={}; plain_bytes={}; socket_error={}; process={}; connection={}",
			result.reason, RUDP_PROTOCOL_VERSION,
			result.receivedVersion ? std::to_string(*result.receivedVersion) : "unavailable",
			result.plainBytes, result.socketError, GetCurrentProcessId(), connectionId));
		return;
	}
	NetBuffer sendBuffer;
	sendBuffer.Init();

	if (!localTlsHelper.ApplyRemainingTimeout(clientSocket)) return;
	const auto session = ReserveSession(sendBuffer, rudpSessionIP);
	if (not SendSessionInfoToClient(clientSocket, localTlsHelper, sendBuffer))
	{
		if (session != nullptr)
		{
			sessionDelegate.AbortReservedSession(*session);
		}
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

RUDPSession* RUDPSessionBroker::ReserveSession(OUT NetBuffer& sendBuffer, const std::string& rudpServerIP) const
{
	CONNECT_RESULT_CODE connectResultCode;
	const auto session = MultiSocketRUDPCoreFunctionDelegate::AcquireSession();
	do
	{
		if (session == nullptr)
		{
			LOG_ERROR("ReserveSession failed: session is nullptr");
			connectResultCode = CONNECT_RESULT_CODE::SERVER_FULL;
			break;
		}

		connectResultCode = InitReserveSession(*session);
	} while (false);

	if (connectResultCode == CONNECT_RESULT_CODE::SUCCESS)
	{
		if (not InitSessionCrypto(*session))
		{
			LOG_ERROR("ReserveSession failed : InitSessionCrypto failed");
			connectResultCode = CONNECT_RESULT_CODE::SESSION_KEY_GENERATION_FAILED;
		}
	}

	sendBuffer.Init();
	sendBuffer << RUDP_PROTOCOL_VERSION << connectResultCode;
	if (connectResultCode == CONNECT_RESULT_CODE::SUCCESS && session != nullptr)
	{
		SetSessionInfoToBuffer(*session, rudpServerIP, sendBuffer);
		sessionDelegate.SetSessionReservedTime(*session, GetTickCount64());
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

bool RUDPSessionBroker::SendSessionInfoToClient(const SOCKET& clientSocket, TLSHelper::TLSHelperServer& localTlsHelper, OUT NetBuffer& sendBuffer)
{
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
		LOG_ERROR("TLS EncryptData failed");
		return false;
	}

	if (not SendAll(clientSocket, encryptedBuffer, encryptedSize, localTlsHelper))
	{
		LOG_ERROR(std::format("RunSessionBrokerThread send failed with error {}", WSAGetLastError()));
		return false;
	}

	if (localTlsHelper.EncryptCloseNotify(encryptedBuffer, sizeof(encryptedBuffer), encryptedSize))
	{
		if (not SendAll(clientSocket, encryptedBuffer, encryptedSize, localTlsHelper))
		{
			LOG_ERROR(std::format("SendSessionInfoToClient send close notify failed with error {}", WSAGetLastError()));
			return false;
		}
	}

	// Application data and close_notify are already sent. Do not extend the budget waiting for peer FIN.
	shutdown(clientSocket, SD_SEND);

	sendBuffer.Init();

	return true;
}

bool RUDPSessionBroker::SendAll(const SOCKET& socket, const char* sendBuffer, const size_t sendSize, TLSHelper::TLSHelperServer& tls)
{
	size_t sent = 0;
	while (sent < sendSize)
	{
		if (!tls.ApplyRemainingTimeout(socket)) return false;
		const int ret = send(socket, sendBuffer + sent, static_cast<int>(sendSize - sent), 0);
		if (ret <= 0)
		{
			return false;
		}

		sent += ret;
	}

	return true;
}
