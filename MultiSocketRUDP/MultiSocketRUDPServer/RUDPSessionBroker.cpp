#include "PreCompile.h"
#include <WinSock2.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <array>
#include "MultiSocketRUDPCore.h"
#include "LogExtension.h"
#include "Logger.h"
#include "../Common/Crypto/CryptoHelper.h"

#if USE_IOCP_SESSION_BROKER
bool MultiSocketRUDPCore::RUDPSessionBroker::Start(const std::wstring& sessionBrokerOptionFilePath)
{
	if (not CNetServer::Start(sessionBrokerOptionFilePath.c_str()))
	{
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = "RUDPSessionBroker Start failed";
		Logger::GetInstance().WriteLog(log);
		return false;
	}

	return true;
}

void MultiSocketRUDPCore::RUDPSessionBroker::Stop()
{
	CNetServer::Stop();
	isServerStopped = true;
}

void MultiSocketRUDPCore::RUDPSessionBroker::OnClientJoin(UINT64 clientId)
{

}

void MultiSocketRUDPCore::RUDPSessionBroker::OnClientLeave(UINT64 sessionId)
{

}

void MultiSocketRUDPCore::RUDPSessionBroker::OnRecv(UINT64 sessionId, NetBuffer* recvBuffer)
{

}

void MultiSocketRUDPCore::RUDPSessionBroker::OnError(st_Error* OutError)
{

}
#else
void MultiSocketRUDPCore::RunSessionBrokerThread(const PortType listenPort, const std::string& rudpSessionIP)
{
	if (tlsHelper.Initialize() == false)
	{
		LOG_ERROR("RunSessionBrokerThread tlsHelper.Initialize failed");
		return;
	}

	SOCKET clientSocket = INVALID_SOCKET;
	sockaddr_in clientAddr;
	if (not OpenSessionBrokerSocket(listenPort))
	{
		LOG_ERROR("RunSessionBrokerThread OpenSessionBrokerSocket failed");
		return;
	}

	int sockAddrSize = sizeof(clientAddr); 
	NetBuffer sendBuffer;
	while (not threadStopFlag)
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

		if (tlsHelper.Handshake(clientSocket) == false)
		{
			LOG_ERROR("RunSessionBrokerThread tlsHelper.Handshake failed");
			closesocket(clientSocket);
			continue;
		}

		ReserveSession(sendBuffer, rudpSessionIP);
		SendSessionInfoToClient(clientSocket, sendBuffer);
	}

	const auto log = Logger::MakeLogObject<ServerLog>();
	log->logString = "Session broker thread stopped";
	Logger::GetInstance().WriteLog(log);
}

bool MultiSocketRUDPCore::OpenSessionBrokerSocket(const PortType listenPort)
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

bool MultiSocketRUDPCore::InitSessionCrypto(OUT RUDPSession& session)
{
	return GenerateSessionKey(session) && GenerateSaltKey(session);
}

bool MultiSocketRUDPCore::GenerateSessionKey(OUT RUDPSession& session)
{
	if (auto bytes = CryptoHelper::GenerateSecureRandomBytes(SESSION_KEY_SIZE); bytes.has_value())
	{
		session.sessionKey.assign(bytes->begin(), bytes->end());
		return true;
	}

	return false;
}

bool MultiSocketRUDPCore::GenerateSaltKey(OUT RUDPSession& session)
{
	if (auto bytes = CryptoHelper::GenerateSecureRandomBytes(SESSION_SALT_SIZE); bytes.has_value())
	{
		session.sessionSalt.assign(bytes->begin(), bytes->end());
		return true;
	}

	return false;
}

void MultiSocketRUDPCore::SetSessionInfoToBuffer(const RUDPSession& session, const std::string& rudpSessionIP, OUT NetBuffer& buffer)
{
	const PortType targetPort = session.serverPort;
	const SessionIdType sessionId = session.sessionId;

	//Send rudp session information packet to client
	buffer << rudpSessionIP << targetPort << sessionId << session.sessionKey << session.sessionSalt;
}

void MultiSocketRUDPCore::ReserveSession(OUT NetBuffer& sendBuffer, const std::string& rudpSessionIP)
{
	CONNECT_RESULT_CODE connectResultCode = CONNECT_RESULT_CODE::SUCCESS;
	const auto session = AcquireSession();
	do
	{
		if (session == nullptr)
		{
			LOG_ERROR("ReserveSession failed : session is nullptr");
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

	sendBuffer << connectResultCode;
	if (connectResultCode == CONNECT_RESULT_CODE::SUCCESS && session != nullptr)
	{
		SetSessionInfoToBuffer(*session, rudpSessionIP, sendBuffer);
		++connectedUserCount;
	}
	else
	{
		if (session != nullptr)
		{
			session->Disconnect();
		}
	}
}

CONNECT_RESULT_CODE MultiSocketRUDPCore::InitReserveSession(RUDPSession& session) const
{
	if (session.isConnected)
	{
		LOG_ERROR("This session already connected");
		return CONNECT_RESULT_CODE::ALREADY_CONNECTED_SESSION;
	}

	if (session.sock = CreateRUDPSocket(); session.sock == INVALID_SOCKET)
	{
		LOG_ERROR(std::format("CreateRUDPSocket failed with error {}", WSAGetLastError()));
		return CONNECT_RESULT_CODE::CREATE_SOCKET_FAILED;
	}
	
	sockaddr_in serverAddr;
	socklen_t len = sizeof(serverAddr);
	getsockname(session.sock, reinterpret_cast<sockaddr*>(&serverAddr), &len);
	session.serverPort = ntohs(serverAddr.sin_port);

	if (session.InitializeRIO(rioFunctionTable, rioCQList[session.threadId], rioCQList[session.threadId]) == false)
	{
		LOG_ERROR(std::format("RUDPSession::InitializeRIO failed with error {}", WSAGetLastError()));
		return CONNECT_RESULT_CODE::RIO_INIT_FAILED;
	}

	if (not DoRecv(session))
	{
		LOG_ERROR(std::format("DoRecv failed with error {}", WSAGetLastError()));
		return CONNECT_RESULT_CODE::DO_RECV_FAILED;
	}

	return CONNECT_RESULT_CODE::SUCCESS;
}

void MultiSocketRUDPCore::SendSessionInfoToClient(const SOCKET& clientSocket, OUT NetBuffer& sendBuffer)
{
	EncodePacket(sendBuffer);
	if (const int result = send(clientSocket, sendBuffer.GetBufferPtr(), sendBuffer.GetAllUseSize(), 0); result == SOCKET_ERROR)
	{
		LOG_ERROR(std::format("RunSessionBrokerThread send failed with error {}", WSAGetLastError()));
	}

	closesocket(clientSocket);
	sendBuffer.Init();
}

#endif
