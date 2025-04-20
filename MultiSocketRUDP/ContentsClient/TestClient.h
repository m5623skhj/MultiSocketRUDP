#pragma once
#include <thread>
#include "NetServerSerializeBuffer.h"
#include "PacketIdType.h"
#include <mutex>
#include <set>

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
	bool Start(const std::wstring& clientCoreOptionFile, const std::wstring& sessionGetterOptionFile);
	void Stop();
	bool IsConnected() const;

private:
	bool WaitingConnectToServer(const unsigned int maximumConnectWaitingCount);
	void RunTestThread();
	bool ProcessPacketHandle(NetBuffer& buffer, const PACKET_ID packetId);
	void SendAnyPacket();
	void SendAnyPacket(const unsigned int sendCount);

private:
	std::thread testThread;

private:
	std::atomic_int order{ 0 };
	std::list<int> orderList{};
	std::mutex orderListLock;
	std::multiset<std::string> echoStringSet{};
	std::mutex echoStringSetLock;
};