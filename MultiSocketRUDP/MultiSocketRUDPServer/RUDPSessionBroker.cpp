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
	SOCKET listenSocket, clientSocket = INVALID_SOCKET;
	sockaddr_in clientAddr;
	if (not OpenSessionBrokerSocket(listenPort, listenSocket))
	{
		return;
	}

	int sockAddrSize = static_cast<int>(sizeof(clientAddr)); 
	auto& sendBuffer = *NetBuffer::Alloc();
	while (not threadStopFlag)
	{
		clientSocket = accept(listenSocket, (sockaddr*)&clientAddr, &sockAddrSize);
		if (clientSocket == INVALID_SOCKET)
		{
			auto log = Logger::MakeLogObject<ServerLog>();
			log->logString = "RunSessionBrokerThread accept falid with error " + WSAGetLastError();
			Logger::GetInstance().WriteLog(log);
			continue;
		}

		ReserveSession(sendBuffer, rudpSessionIP);
		SendSessionInfoToClient(clientSocket, sendBuffer);
	}

	closesocket(listenSocket);
	NetBuffer::Free(&sendBuffer);

	auto log = Logger::MakeLogObject<ServerLog>();
	log->logString = "Session broker thread stopped";
	Logger::GetInstance().WriteLog(log);
}

bool MultiSocketRUDPCore::OpenSessionBrokerSocket(const PortType listenPort, OUT SOCKET& listenSocket)
{
	listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listenSocket == INVALID_SOCKET)
	{
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = "RunSessionBrokerThread listen socket is invalid with error " + WSAGetLastError();
		Logger::GetInstance().WriteLog(log);

		return false;
	}

	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.S_un.S_addr = INADDR_ANY;
	serverAddr.sin_port = htons(listenPort);

	if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
	{
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = "RunSessionBrokerThread bind failed with error " + WSAGetLastError();
		Logger::GetInstance().WriteLog(log);

		closesocket(listenSocket);
		return false;
	}

	if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
	{
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = "RunSessionBrokerThread listen failed with error " + WSAGetLastError();
		Logger::GetInstance().WriteLog(log);

		closesocket(listenSocket);
		return false;
	}

	return true;
}

void MultiSocketRUDPCore::SetSessionKey(OUT RUDPSession& session)
{
	auto MakeSessionKey = []() -> std::string
	{
		std::array<unsigned char, sessionKeySize> keyData;

		auto now = std::chrono::system_clock::now();
		auto duration = now.time_since_epoch();
		auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

		std::seed_seq seed { static_cast<unsigned int>(nowMs & 0xFFFFFFFF), static_cast<unsigned int>((nowMs >> 32) & 0xFFFFFFFF) };
		std::mt19937 gen(seed);
		std::uniform_int_distribution<int> dist(0, 255);

		for (auto& byte : keyData)
		{
			byte = static_cast<unsigned char>(dist(gen));
		}

		std::stringstream ss;
		for (auto byte : keyData)
		{
			ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
		}

		return ss.str();
	};
	session.sessionKey = MakeSessionKey();
}

void MultiSocketRUDPCore::SetSessionInfoToBuffer(RUDPSession& session, const std::string& rudpSessionIP, OUT NetBuffer& buffer)
{
	PortType targetPort = session.serverPort;
	SessionIdType sessionId = session.sessionId;
	std::string sessionKey = session.sessionKey;

	//Send rudp session infomation packet to client
	buffer << rudpSessionIP << targetPort << sessionId << sessionKey;
}

void MultiSocketRUDPCore::ReserveSession(OUT NetBuffer& sendBuffer, const std::string& rudpSessionIP)
{
	char connectResultCode = 0;
	auto session = AcquireSession();
	do
	{
		if (session == nullptr)
		{
			auto log = Logger::MakeLogObject<ServerLog>();
			log->logString = "Server is full of users";
			Logger::GetInstance().WriteLog(log);
			connectResultCode = 1;
			sendBuffer << connectResultCode;

			break;
		}

		if (session->isConnected)
		{
			auto log = Logger::MakeLogObject<ServerLog>();
			log->logString = "This session already connected";
			Logger::GetInstance().WriteLog(log);
			connectResultCode = 2;
			sendBuffer << connectResultCode;

			break;
		}
	} while (false);
	
	sendBuffer << connectResultCode;
	if (connectResultCode == 0)
	{
		SetSessionKey(*session);
		SetSessionInfoToBuffer(*session, rudpSessionIP, sendBuffer);
		++connectedUserCount;
	}
}

void MultiSocketRUDPCore::SendSessionInfoToClient(OUT SOCKET& clientSocket, OUT NetBuffer& sendBuffer)
{
	EncodePacket(sendBuffer);
	int result = send(clientSocket, sendBuffer.GetBufferPtr(), sendBuffer.GetAllUseSize(), 0);
	if (result == SOCKET_ERROR)
	{
		auto log = Logger::MakeLogObject<ServerLog>();
		log->logString = "RunSessionBrokerThread send failed with error " + WSAGetLastError();
		Logger::GetInstance().WriteLog(log);
	}

	closesocket(clientSocket);
	sendBuffer.Init();
}

#endif
