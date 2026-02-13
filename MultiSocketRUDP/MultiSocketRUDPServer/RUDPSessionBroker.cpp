#include "PreCompile.h"
#include "RUDPSessionBroker.h"
#include "RUDPSession.h"
#include "MultiSocketRUDPCore.h"
#include "LogExtension.h"
#include "Logger.h"
#include "MultiSocketRUDPCoreFuntionDeletage.h"
#include "RUDPSessionFunctionDelegate.h"
#include "../Common/Crypto/CryptoHelper.h"
#include "../Common/PacketCrypto/PacketCryptoHelper.h"

RUDPSessionBroker::RUDPSessionBroker(
	MultiSocketRUDPCore& inCore,
	const std::wstring& certStoreName,
	const std::wstring& certSubjectName)
	: core(inCore)
	, tlsHelper(certStoreName, certSubjectName)
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

	if (not tlsHelper.Initialize())
	{
		LOG_ERROR("RUDPSessionBroker tlsHelper initialize failed");
		return false;
	}

	if (not OpenSessionBrokerSocket(listenPort))
	{
		LOG_ERROR("RUDPSessionBroker OpenSessionBrokerSocket failed");
		return false;
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

	NetBuffer sendBuffer;
	while (not stopToken.stop_requested())
	{
		sendBuffer.Init();

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

		if (tlsHelper.Handshake(clientSocket) == false)
		{
			LOG_ERROR("RunSessionBrokerThread tlsHelper.Handshake failed");
			closesocket(clientSocket);
			continue;
		}

		if (const auto session = ReserveSession(sendBuffer, rudpSessionIP); session != nullptr)
		{
			if (not SendSessionInfoToClient(clientSocket, sendBuffer))
			{
				RUDPSessionFunctionDelegate::AbortReservedSession(*session);
			}
		}
	}

	const auto log = Logger::MakeLogObject<ServerLog>();
	log->logString = "Session broker thread stopped";
	Logger::GetInstance().WriteLog(log);
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

RUDPSession* RUDPSessionBroker::ReserveSession(OUT NetBuffer& sendBuffer, const std::string& rudpServerIP)
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
	sendBuffer << connectResultCode;
	if (connectResultCode == CONNECT_RESULT_CODE::SUCCESS && session != nullptr)
	{
		SetSessionInfoToBuffer(*session, rudpServerIP, sendBuffer);
	}
	else
	{
		if (session != nullptr)
		{
			session->DoDisconnect();
		}
	}

	RUDPSessionFunctionDelegate::SetSessionReservedTime(*session, GetTickCount64());
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

bool RUDPSessionBroker::InitSessionCrypto(OUT RUDPSession& session)
{
	if (not GenerateSessionKey(session) || not GenerateSaltKey(session))
	{
		return false;
	}

	if (RUDPSessionFunctionDelegate::GetSessionKeyHandle(session) == nullptr)
	{
		RUDPSessionFunctionDelegate::SetSessionKeyObjectBuffer(session, new unsigned char[CryptoHelper::GetTLSInstance().GetKeyOjbectSize()]);
	}

	RUDPSessionFunctionDelegate::SetSessionKeyHandle(session, 
		CryptoHelper::GetTLSInstance().GetSymmetricKeyHandle(
			RUDPSessionFunctionDelegate::GetSessionKeyObjectBuffer(session),
			const_cast<unsigned char*>(RUDPSessionFunctionDelegate::GetSessionKey(session))));
	if (RUDPSessionFunctionDelegate::GetSessionKeyHandle(session) == nullptr)
	{
		LOG_ERROR("InitSessionCrypto failed : GetSymmetricKeyHandle failed");
		return false;
	}

	return true;
}

bool RUDPSessionBroker::GenerateSessionKey(OUT RUDPSession& session)
{
	if (const auto bytes = CryptoHelper::GenerateSecureRandomBytes(SESSION_KEY_SIZE); bytes.has_value())
	{
		RUDPSessionFunctionDelegate::SetSessionKey(session, bytes->data());
		return true;
	}

	return false;
}

bool RUDPSessionBroker::GenerateSaltKey(OUT RUDPSession& session)
{
	if (const auto bytes = CryptoHelper::GenerateSecureRandomBytes(SESSION_SALT_SIZE); bytes.has_value())
	{
		RUDPSessionFunctionDelegate::SetSessionSalt(session, bytes->data());
		return true;
	}

	return false;
}

void RUDPSessionBroker::SetSessionInfoToBuffer(const RUDPSession& session, const std::string& rudpServerIP, OUT NetBuffer& buffer)
{
	PortType targetPort;
	SessionIdType sessionId;
	RUDPSessionFunctionDelegate::GetServerPortAndSessionId(session, targetPort, sessionId);

	//Send rudp session information packet to client
	buffer << rudpServerIP << targetPort << sessionId;
	buffer.WriteBuffer(RUDPSessionFunctionDelegate::GetSessionKey(session), SESSION_KEY_SIZE);
	buffer.WriteBuffer(RUDPSessionFunctionDelegate::GetSessionSalt(session), SESSION_SALT_SIZE);
}

bool RUDPSessionBroker::SendSessionInfoToClient(const SOCKET& clientSocket, OUT NetBuffer& sendBuffer)
{
	PacketCryptoHelper::SetHeader(sendBuffer);

	constexpr size_t maxTlsPacketSize = 16 * 1024 + 512;
	char encryptedBuffer[maxTlsPacketSize];
	size_t encryptedSize = 0;
	constexpr DWORD tlsShutdownTimeout = 300;
	constexpr size_t tlsFinBufferSize = 256;

	if (not tlsHelper.EncryptData(
		sendBuffer.GetBufferPtr(),
		sendBuffer.GetAllUseSize(),
		encryptedBuffer,
		encryptedSize))
	{
		LOG_ERROR("TLS EncryptData failed");
		return false;
	}

	if (not SendAll(clientSocket, encryptedBuffer, encryptedSize))
	{
		LOG_ERROR(std::format("RunSessionBrokerThread send failed with error {}", WSAGetLastError()));
		return false;
	}

	if (tlsHelper.EncryptCloseNotify(encryptedBuffer, sizeof(encryptedBuffer), encryptedSize))
	{
		if (not SendAll(clientSocket, encryptedBuffer, encryptedSize))
		{
			LOG_ERROR(std::format("RunSessionBrokerThread send close notify failed with error {}", WSAGetLastError()));
			return false;
		}
	}

	setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tlsShutdownTimeout), sizeof(tlsShutdownTimeout));
	shutdown(clientSocket, SD_SEND);

	while (true)
	{
		if (char finRecv[tlsFinBufferSize]; recv(clientSocket, finRecv, sizeof(finRecv), 0) <= 0)
		{
			break;
		}
	}

	closesocket(clientSocket);
	sendBuffer.Init();

	return true;
}

bool RUDPSessionBroker::SendAll(const SOCKET& socket, const char* sendBuffer, const size_t sendSize)
{
	size_t sent = 0;
	while (sent < sendSize)
	{
		const int ret = send(socket, sendBuffer + sent, static_cast<int>(sendSize - sent), 0);
		if (ret == SOCKET_ERROR)
		{
			return false;
		}

		sent += ret;
	}

	return true;
}