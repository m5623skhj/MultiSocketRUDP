#pragma once
#include <optional>
#include <vector>
#include <thread>
#include "NetServer.h"
#include "NetServerSerializeBuffer.h"

#pragma comment(lib, "ws2_32.lib")


class MultiSocketRUDPCore
{
public:
	MultiSocketRUDPCore();
	virtual ~MultiSocketRUDPCore() = default;

public:
	[[nodiscard]]
	bool StartServer(const std::wstring& optionFilePath, const std::wstring& sessionBrokerOptionFilePath);
	void StopServer();

	[[nodiscard]]
	bool IsServerStopped();

private:
	[[nodiscard]]
	bool InitNetwork();
	bool RunAllThreads();
	void RunSessionBroker();
	std::optional<SOCKET> CreateRUDPSocket(unsigned short socketNumber);

	void CloseAllSockets();

private:
	bool threadStopFlag{};
	bool isServerStopped{};
	unsigned short numOfSockets{};
	unsigned short portStartNumber{};
	unsigned short sessionBrokerPort{};
	std::vector<SOCKET> socketList;

private:
#if USE_IOCP_SESSION_BROKER
	class RUDPSessionBroker : public CNetServer
	{
		friend MultiSocketRUDPCore;

	private:
		RUDPSessionBroker() = default;
		~RUDPSessionBroker() = default;

	private:
		bool Start(const std::wstring& sessionBrokerOptionFilePath);
		void Stop();

	private:
		void OnClientJoin(UINT64 sessionId) override;
		void OnClientLeave(UINT64 sessionId) override;
		bool OnConnectionRequest(const WCHAR* ip)  override { UNREFERENCED_PARAMETER(ip); return true; }
		void OnRecv(UINT64 sessionId, NetBuffer* recvBuffer) override;
		void OnSend(UINT64 sessionId, int sendSize) override { UNREFERENCED_PARAMETER(sessionId); UNREFERENCED_PARAMETER(sendSize); }
		void OnWorkerThreadBegin() override {}
		void OnWorkerThreadEnd() override {}
		void OnError(st_Error* OutError) override;
		void GQCSFailed(int lastError, UINT64 sessionId) override { UNREFERENCED_PARAMETER(lastError); UNREFERENCED_PARAMETER(sessionId); }

	private:
		bool isServerStopped{};
	};
	RUDPSessionBroker sessionBroker;
#else
	void RunSessionBrokerThread(unsigned short listenPort);

private:
	std::thread sessionBrokerThread{};
#endif
};