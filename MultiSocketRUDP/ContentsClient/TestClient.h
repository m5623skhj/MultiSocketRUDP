#pragma once
#include <thread>
#include "NetServerSerializeBuffer.h"

class TestClient
{
private:
	TestClient() = default;
	~TestClient() = default;
	TestClient& operator=(const TestClient&) = delete;
	TestClient(TestClient&&) = delete;

public:
	static TestClient& GetInst();

public:
	bool Start(const std::wstring& optionFilePath);
	void Stop();

private:
	void RunTestThread();
	bool ProcessPacketHandle(NetBuffer& buffer);

private:
	std::thread testThread;
};