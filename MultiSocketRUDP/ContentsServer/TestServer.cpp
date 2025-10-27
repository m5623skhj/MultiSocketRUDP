#include "PreCompile.h"
#include "TestServer.h"
#include "Logger.h"
#include "Player.h"
#include "../Common/TLS/TLSHelper.h"

TestServer::TestServer()
	: serverCore(TLSHelper::StoreNames::MY, L"DevServerCert")
{
}

TestServer& TestServer::GetInst()
{
	static TestServer instance;
	return instance;
}

bool TestServer::Start(const std::wstring& coreOptionFilePath, const std::wstring& sessionBrokerOptionFilePath)
{
	auto playerFactoryFunc = [](MultiSocketRUDPCore& inCore)
	{
		return new Player(inCore);
	};

	if (not serverCore.StartServer(coreOptionFilePath, sessionBrokerOptionFilePath, std::move(playerFactoryFunc), true))
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