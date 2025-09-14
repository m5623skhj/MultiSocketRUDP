#pragma once
#include <thread>
#include "NetServerSerializeBuffer.h"
#include "PacketIdType.h"
#include <mutex>
#include <set>
#include "RUDPClientCore.h"

class TestClient : public RUDPClientCore
{
public:
	TestClient() = default;
	~TestClient() override = default;
	TestClient& operator=(const TestClient&) = delete;
	TestClient(TestClient&&) = delete;

public:
	bool Start(const std::wstring& clientCoreOptionFile, const std::wstring& sessionGetterOptionFile, bool printLogToConsole) override;
	void Stop() override;

private:
	bool WaitingConnectToServer(unsigned int maximumConnectWaitingCount) const;
	void RunTestThread();
	bool ProcessPacketHandle(NetBuffer& buffer, PACKET_ID packetId);
	void SendAnyPacket();
	void SendAnyPacket(unsigned int sendCount);

private:
	std::thread testThread;

private:
	std::atomic_int order{ 0 };
	std::list<int> orderList{};
	std::mutex orderListLock;
	std::multiset<std::string> echoStringSet{};
	std::mutex echoStringSetLock;
};