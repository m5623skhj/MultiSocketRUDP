#pragma once
#include <thread>

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

private:
	std::thread testThread;
};