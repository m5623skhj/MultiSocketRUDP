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

	int32_t GetNumOfPlayers() const;
	int32_t GetNumOfConnected() const;
	int32_t GetNumOfDisconnected() const;
	int32_t GetNumOfDisconnectedByRetransmssion() const;
	int32_t GetTPS() const;
	int32_t GetNumOfError() const;

	void ResetTPS() const;

private:
	MultiSocketRUDPCore serverCore;
};