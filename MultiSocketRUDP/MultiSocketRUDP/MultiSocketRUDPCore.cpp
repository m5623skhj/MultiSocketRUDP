#include "PreCompile.h"
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
		CloseAllSessions();
		std::cout << "InitNetwork failed" << std::endl;
		return false;
	}

	if (not RunAllThreads())
	{
		StopServer();
		std::cout << "RunAllThreads() failed" << std::endl;
		return false;
	}

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
	CloseAllSessions();

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

	usingSessionMap.reserve(numOfSockets);
	for (auto socketNumber = 0; socketNumber < numOfSockets; ++socketNumber)
	{
		auto optSocket = CreateRUDPSocket(socketNumber);
		if (optSocket.has_value() == false)
		{
			WSACleanup();
			return false;
		}

		SessionIdType createdSessionId = static_cast<SessionIdType>(socketNumber);
		unusedSessionList.emplace_back(RUDPSession::Create(createdSessionId, optSocket.value(), static_cast<PortType>(portStartNumber + socketNumber)));
	}

	return true;
}

bool MultiSocketRUDPCore::RunAllThreads()
{
	RunSessionBroker();
	workerThreads.reserve(numOfWorkerThread);

	for (ThreadIdType id = 0; id < numOfWorkerThread; ++id)
	{
		workerThreads.emplace_back([this, id]() { this->RunWorkerThread(id); });
	}

	return true;
}

void MultiSocketRUDPCore::RunSessionBroker()
{
#if USE_IOCP_SESSION_BROKER
	if (not sessionBroker.Start(sessionBrokerOptionFilePath))
	{
		CloseAllSockets();
		std::cout << "SessionBroker start falied" << std::endl;
		return false;
	}
#else
	sessionBrokerThread = std::thread([this]() { this->RunSessionBrokerThread(sessionBrokerPort, ip); });
#endif
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

void MultiSocketRUDPCore::CloseAllSessions()
{
	{
		std::unique_lock lock(usingSessionMapLock);
		usingSessionMap.clear();
	}
	{
		std::scoped_lock lock(unusedSessionListLock);
		unusedSessionList.clear();
	}
}

std::shared_ptr<RUDPSession> MultiSocketRUDPCore::AcquireSession()
{
	std::shared_ptr<RUDPSession> session = nullptr;
	{
		std::scoped_lock lock(unusedSessionListLock);
		
		if (unusedSessionList.empty() == true)
		{
			return nullptr;
		}

		session = unusedSessionList.front();
		unusedSessionList.pop_front();
	}

	session->isUsingSession = true;

	{
		std::unique_lock lock(usingSessionMapLock);
		usingSessionMap.insert({ session->sessionId, session });
	}

	return session;
}

void MultiSocketRUDPCore::ReleaseSession(std::shared_ptr<RUDPSession> session)
{
	if (session == nullptr)
	{
		std::cout << "ReleaseSession() : session is nullptr" << std::endl;
		return;
	}
	
	{
		std::unique_lock lock(usingSessionMapLock);
		usingSessionMap.erase(session->sessionId);
	}
	
	session->isUsingSession = false;

	{
		std::scoped_lock lock(unusedSessionListLock);
		unusedSessionList.push_back(session);
	}
}

void MultiSocketRUDPCore::RunWorkerThread(ThreadIdType threadId)
{
	while (not threadStopFlag)
	{

	}

	std::cout << "worker thread stopped" << std::endl;
}