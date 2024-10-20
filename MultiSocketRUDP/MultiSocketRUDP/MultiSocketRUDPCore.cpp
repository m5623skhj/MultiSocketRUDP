#include <PreCompile.h>
#include <WinSock2.h>
#include "MultiSocketRUDPCore.h"

MultiSocketRUDPCore::MultiSocketRUDPCore()
{
}

bool MultiSocketRUDPCore::StartServer(const std::wstring& optionFilePath, const std::wstring& sessionBrokerOptionFilePath)
{
	// Parsing items from option file path

	if (not InitNetwork())
	{
		CloseAllSockets();
		std::cout << "InitNetwork failed" << std::endl;
		return false;
	}

#if USE_IOCP_SESSION_BROKER
	if (not sessionBroker.Start(sessionBrokerOptionFilePath))
	{
		CloseAllSockets();
		std::cout << "SessionBroker start falied" << std::endl;
		return false;
	}
#else
	sessionBrokerThread = std::thread([this]() { this->RunSessionBrokerThread(sessionBrokerPort); });
#endif

	return true;
}

void MultiSocketRUDPCore::StopServer()
{
	threadStopFlag = true;

#if USE_IOCP_SESSION_BROKER
	sessionBroker.Stop();
#else
	sessionBrokerThread.join();
#endif
	CloseAllSockets();

	isServerStopped = true;
	std::cout << "Server stop" << std::endl;
}

bool MultiSocketRUDPCore::IsServerStopped()
{
	return isServerStopped;
}

bool MultiSocketRUDPCore::InitNetwork()
{
	WSADATA wsaData;
	int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != 0)
	{
		std::cout << "WSAStartup failed " << result << std::endl;
		return false;
	}

	for (auto socketNumber = 0; socketNumber < numOfSockets; ++socketNumber)
	{
		auto optSocket = CreateRUDPSocket(socketNumber);
		if (optSocket.has_value() == false)
		{
			WSACleanup();
			return false;
		}

		socketList.emplace_back(optSocket.value());
	}

	return true;
}

bool MultiSocketRUDPCore::RunAllThreads()
{
	return true;
}

std::optional<SOCKET> MultiSocketRUDPCore::CreateRUDPSocket(unsigned short socketNumber)
{
	SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == INVALID_SOCKET)
	{
		std::cout << "Socket create failed " << WSAGetLastError() << std::endl;
		return std::nullopt;
	}

	sockaddr_in serverAddr;
	memset(&serverAddr, 0, sizeof(serverAddr));

	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.S_un.S_addr = INADDR_ANY;
	serverAddr.sin_port = htons(portStartNumber + socketNumber);

	if (bind(sock, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
	{
		std::cout << "Bind failed " << WSAGetLastError() << std::endl;
		closesocket(sock);
		return std::nullopt;
	}

	return sock;
}

void MultiSocketRUDPCore::CloseAllSockets()
{
	for (auto& socket : socketList)
	{
		closesocket(socket);
	}
}

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
void MultiSocketRUDPCore::RunSessionBrokerThread(unsigned short listenPort)
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

	listen(listenSocket, SOMAXCONN);
	while (not threadStopFlag)
	{
		clientSocket = accept(listenSocket, (sockaddr*)&clientAddr, &sockAddrSize);
		if (clientSocket == INVALID_SOCKET)
		{
			std::cout << "RunSessionBrokerThread accept falid with error " << WSAGetLastError() << std::endl;
			continue;
		}

		//Send rudp session infomation packet to client
		//...
		//int result = send(clientSocket, , , 0);
		//if (result == SOCKET_ERROR) 
		//{
		//	std::cout << "RunSessionBrokerThread send failed with error " << WSAGetLastError() << std::endl;
		//}

		closesocket(clientSocket);
	}

	closesocket(listenSocket);
	std::cout << "Session broker thread stopped" << std::endl;
}
#endif
