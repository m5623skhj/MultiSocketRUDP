#include "PreCompile.h"
#include <WinSock2.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <array>
#include "MultiSocketRUDPCore.h"

#if USE_IOCP_SESSION_BROKER
bool MultiSocketRUDPCore::RUDPSessionBroker::Start(const std::wstring& sessionBrokerOptionFilePath)
{
	if (not CNetServer::Start(sessionBrokerOptionFilePath.c_str()))
	{
		std::cout << "RUDPSessionBroker Start failed" << std::endl;
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
void MultiSocketRUDPCore::RunSessionBrokerThread(PortType listenPort, std::string rudpSessionIP)
{
	SOCKET listenSocket, clientSocket = INVALID_SOCKET;

	listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listenSocket == INVALID_SOCKET)
	{
		std::cout << "RunSessionBrokerThread listen socket is invalid with error " << WSAGetLastError() << std::endl;
		return;
	}

	sockaddr_in serverAddr, clientAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.S_un.S_addr = INADDR_ANY;
	serverAddr.sin_port = htons(listenPort);
	int sockAddrSize = static_cast<int>(sizeof(clientAddr));

	if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
	{
		std::cerr << "RunSessionBrokerThread bind failed with error " << WSAGetLastError() << std::endl;
		closesocket(listenSocket);
		return;
	}

	auto& sendBuffer = *NetBuffer::Alloc();
	listen(listenSocket, SOMAXCONN);
	while (not threadStopFlag)
	{
		clientSocket = accept(listenSocket, (sockaddr*)&clientAddr, &sockAddrSize);
		if (clientSocket == INVALID_SOCKET)
		{
			std::cout << "RunSessionBrokerThread accept falid with error " << WSAGetLastError() << std::endl;
			continue;
		}

		auto session = AcquireSession();
		if (session == nullptr)
		{
			std::cout << "Server is full of users" << std::endl;
			continue;
		}
		
		if (session->isConnected)
		{
			std::cout << "This session already connected" << std::endl;
			continue;
		}

		if (not SetSessionId(session))
		{
			std::cout << "Session already has session id" << std::endl;
			continue;
		}

		SetSessionKey(session);
		SetSessionInfoToBuffer(session, rudpSessionIP, sendBuffer);

		int result = send(clientSocket, sendBuffer.GetBufferPtr(), sendBuffer.GetUseSize() + df_HEADER_SIZE, 0);
		if (result == SOCKET_ERROR)
		{
			std::cout << "RunSessionBrokerThread send failed with error " << WSAGetLastError() << std::endl;
			continue;
		}

		closesocket(clientSocket);
		sendBuffer.Init();
	}

	closesocket(listenSocket);
	std::cout << "Session broker thread stopped" << std::endl;
}

bool MultiSocketRUDPCore::SetSessionId(OUT std::shared_ptr<RUDPSession> session)
{
	if (session->sessionId != invalidSessionId)
	{
		return false;
	}

	session->sessionId = ++sessionIdGenerator;
	return true;
}

void MultiSocketRUDPCore::SetSessionKey(OUT std::shared_ptr<RUDPSession> session)
{
	auto MakeSessionKey = []() -> std::string
	{
		std::array<unsigned char, sessionKeySize> keyData;

		auto now = std::chrono::system_clock::now();
		auto duration = now.time_since_epoch();
		auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

		std::seed_seq seed { static_cast<unsigned int>(millis & 0xFFFFFFFF), static_cast<unsigned int>((millis >> 32) & 0xFFFFFFFF) };
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
	session->sessionKey = MakeSessionKey();
}

void MultiSocketRUDPCore::SetSessionInfoToBuffer(std::shared_ptr<RUDPSession> session, const std::string& rudpSessionIP, OUT NetBuffer& buffer)
{
	PortType targetPort = session->serverPort;
	SessionIdType sessionId = session->sessionId;
	std::string sessionKey = session->sessionKey;

	//Send rudp session infomation packet to client
	buffer << rudpSessionIP << targetPort << sessionId << sessionKey;
	EncodePacket(buffer);
}

#endif
