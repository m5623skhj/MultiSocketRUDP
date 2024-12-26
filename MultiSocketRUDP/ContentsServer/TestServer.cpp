#include "PreCompile.h"
#include "TestServer.h"

TestServer& TestServer::GetInst()
{
	static TestServer instance;
	return instance;
}

bool TestServer::Start(const std::wstring& coreOptionFilePath, const std::wstring& sessionBrokerOptionFilePath)
{
	if (not serverCore.StartServer(coreOptionFilePath, sessionBrokerOptionFilePath))
	{
		std::cout << "StartServer() failed" << std::endl;
		return 0;
	}

	std::cout << "Server is running" << std::endl;
	return true;
}

void TestServer::Stop()
{
	serverCore.StopServer();

	std::cout << "Server stopped" << std::endl;
}
