#pragma once
#include "MultiSocketRUDPCore.h"

class TestServer
{
private:
	TestServer();
	~TestServer() = default;
	TestServer& operator=(const TestServer&) = delete;
	TestServer(TestServer&&) = delete;

public:
	static TestServer& GetInst();

public:
	bool Start(const std::wstring& coreOptionFilePath, const std::wstring& sessionBrokerOptionFilePath);
	void Stop();

public:
	bool IsServerStopped() const;

private:
	MultiSocketRUDPCore serverCore;
};