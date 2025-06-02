#include "PreCompile.h"
#include "TestServer.h"
#include "Logger.h"

TestServer& TestServer::GetInst()
{
	static TestServer instance;
	return instance;
}

bool TestServer::Start(const std::wstring& coreOptionFilePath, const std::wstring& sessionBrokerOptionFilePath)
{
	if (not serverCore.StartServer(coreOptionFilePath, sessionBrokerOptionFilePath, true))
	{
		std::cout << "StartServer() failed" << std::endl;
		Logger::GetInstance().StopLoggerThread();
		return false;
	}

	std::cout << "Server is running" << std::endl;
	return true;
}

void TestServer::Stop()
{
	serverCore.StopServer();

	std::cout << "Server stopped" << std::endl;
}

bool TestServer::IsServerStopped() const
{
	return serverCore.IsServerStopped();
}