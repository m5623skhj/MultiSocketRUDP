#include "PreCompile.h"
#include "TestClient.h"
#include "RUDPClientCore.h"

TestClient& TestClient::GetInst()
{
	static TestClient instance;
	return instance;
}

bool TestClient::Start(const std::wstring& optionFilePath)
{
	if (not RUDPClientCore::GetInst().Start(optionFilePath))
	{
		std::cout << "Core start failed" << std::endl;
		return false;
	}

	testThread = std::thread{ &TestClient::RunTestThread, this };

	std::cout << "Client is running" << std::endl;
	return true;
}

void TestClient::Stop()
{
	RUDPClientCore::GetInst().Stop();
	testThread.join();

	std::cout << "Client stopped" << std::endl;
}

void TestClient::RunTestThread()
{
	while (RUDPClientCore::GetInst().IsStopped())
	{

	}

	std::cout << "Test thread stopped" << std::endl;
}
