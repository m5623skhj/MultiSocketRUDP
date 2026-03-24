#include "PreCompile.h"
#include "TestServer.h"
#include "Logger.h"
#include "Player.h"
#include "../Common/TLS/TLSHelper.h"
#include "../MultiSocketRUDPServer/LogExtension.h"

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

	if (not serverCore.StartServer(coreOptionFilePath, sessionBrokerOptionFilePath, std::move(playerFactoryFunc), false))
	{
		std::cout << "StartServer() failed" << '\n';
		Logger::GetInstance().StopLoggerThread();
		return false;
	}

	std::cout << "Server is running" << '\n';
	return true;
}

void TestServer::Stop()
{
	serverCore.StopServer();

	std::cout << "Server stopped" << '\n';
}

bool TestServer::IsServerStopped() const
{
	return serverCore.IsServerStopped();
}

int32_t TestServer::GetNumOfPlayers() const
{
	return serverCore.GetNowSessionCount();
}

int32_t TestServer::GetNumOfConnected() const
{
	return serverCore.GetAllConnectedCount();
}

int32_t TestServer::GetNumOfDisconnected() const
{
	return serverCore.GetAllDisconnectedCount();
}

int32_t TestServer::GetNumOfDisconnectedByRetransmssion() const
{
	return serverCore.GetAllDisconnectedByRetransmissionCount();
}

int32_t TestServer::GetTPS() const
{
	return serverCore.GetTPS();
}

int32_t TestServer::GetNumOfError() const
{
	return numOfOccurredError.load(std::memory_order_relaxed);
}

void TestServer::ResetTPS() const
{
	serverCore.ResetTPS();
}
