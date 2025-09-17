#include "PreCompile.h"
#include <WinSock2.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <array>
#include "MultiSocketRUDPCore.h"
#include "LogExtension.h"
#include "Logger.h"


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
	SOCKET clientSocket = INVALID_SOCKET;
	sockaddr_in clientAddr;
	if (not OpenSessionBrokerSocket(listenPort))
	{
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

void MultiSocketRUDPCore::SetSessionKey(OUT RUDPSession& session)
{
	auto makeSessionKey = []() -> std::string
	{
		std::array<unsigned char, SESSION_KEY_SIZE> keyData;

		const auto now = std::chrono::system_clock::now();
		const auto duration = now.time_since_epoch();
		const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

		std::seed_seq seed { static_cast<unsigned int>(nowMs & 0xFFFFFFFF), static_cast<unsigned int>((nowMs >> 32) & 0xFFFFFFFF) };
		std::mt19937 gen(seed);
		std::uniform_int_distribution dist(0, 255);

		for (auto& byte : keyData)
		{
			byte = static_cast<unsigned char>(dist(gen));
		}

		std::stringstream ss;
		for (const auto byte : keyData)
		{
			ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
		}

		return ss.str();
	};
	session.sessionKey = makeSessionKey();
}

void MultiSocketRUDPCore::SetSessionInfoToBuffer(const RUDPSession& session, const std::string& rudpSessionIP, OUT NetBuffer& buffer)
{
	const PortType targetPort = session.serverPort;
	const SessionIdType sessionId = session.sessionId;
	std::string sessionKey = session.sessionKey;

	//Send rudp session information packet to client
	buffer << rudpSessionIP << targetPort << sessionId << sessionKey;
}

void MultiSocketRUDPCore::ReserveSession(OUT NetBuffer& sendBuffer, const std::string& rudpSessionIP)
{
	char connectResultCode = 0;
	const auto session = AcquireSession();
	do
	{
		if (session == nullptr)
		{
			LOG_ERROR("ReserveSession failed : session is nullptr");
			break;
		}

		connectResultCode = InitReserveSession(*session);
	} while (false);
	
	sendBuffer << connectResultCode;
	if (connectResultCode == 0)
	{
		SetSessionKey(*session);
		SetSessionInfoToBuffer(*session, rudpSessionIP, sendBuffer);
		++connectedUserCount;
	}
	else
	{
		if (session != nullptr && session->sock != INVALID_SOCKET)
		{
			session->Disconnect();
		}
	}
}

char MultiSocketRUDPCore::InitReserveSession(RUDPSession& session) const
{
	if (session.isConnected)
	{
		LOG_ERROR("This session already connected");
		return 2;
	}

	if (session.sock = CreateRUDPSocket(); session.sock == INVALID_SOCKET)
	{
		LOG_ERROR(std::format("CreateRUDPSocket failed with error {}", WSAGetLastError()));
		return 3;
	}
	
	sockaddr_in serverAddr;
	socklen_t len = sizeof(serverAddr);
	getsockname(session.sock, reinterpret_cast<sockaddr*>(&serverAddr), &len);
	session.serverPort = ntohs(serverAddr.sin_port);

	if (session.InitializeRIO(rioFunctionTable, rioCQList[session.threadId], rioCQList[session.threadId]) == false)
	{
		LOG_ERROR(std::format("RUDPSession::InitializeRIO failed with error {}", WSAGetLastError()));
		return 4;
	}

	if (not DoRecv(session))
	{
		LOG_ERROR(std::format("DoRecv failed with error {}", WSAGetLastError()));
		return 5;
	}

	return 0;
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
