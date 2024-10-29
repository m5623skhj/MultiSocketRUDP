#include "PreCompile.h"
#include <WinSock2.h>
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

	auto &buffer = *NetBuffer::Alloc();
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

		PortType targetPort = session->serverPort;
		SessionIdType sessionId = session->sessionId;

		//Send rudp session infomation packet to client
		buffer << rudpSessionIP << targetPort << sessionId;

		buffer.m_iWriteLast = buffer.m_iWrite;
		buffer.m_iWrite = 0;
		buffer.m_iRead = 0;
		buffer.Encode();

		int result = send(clientSocket, buffer.GetBufferPtr(), buffer.GetUseSize() + df_HEADER_SIZE, 0);
		if (result == SOCKET_ERROR)
		{
			std::cout << "RunSessionBrokerThread send failed with error " << WSAGetLastError() << std::endl;
			continue;
		}

		closesocket(clientSocket);
		buffer.Init();
	}

	closesocket(listenSocket);
	std::cout << "Session broker thread stopped" << std::endl;
}
#endif
