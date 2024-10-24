#pragma once
#include <optional>
#include <list>
#include <thread>
#include "NetServer.h"
#include "NetServerSerializeBuffer.h"
#include "CoreType.h"
#include <unordered_map>
#include <shared_mutex>

#pragma comment(lib, "ws2_32.lib")

class MultiSocketRUDPCore;

class RUDPSession
{
	friend MultiSocketRUDPCore;

private:
	RUDPSession() = delete;
	explicit RUDPSession(SessionIdType inSessionId, SOCKET inSock, PortType inPort);

private:
	static std::shared_ptr<RUDPSession> Create(SessionIdType inSessionId, SOCKET inSock, PortType inPort)
	{
		struct RUDPSessionCreator : public RUDPSession 
		{ 
			RUDPSessionCreator(SessionIdType inSessionId, SOCKET inSock, PortType inPort)
				: RUDPSession(inSessionId, inSock, inPort)
			{
			}
		};

		return std::make_shared<RUDPSessionCreator>(inSessionId, inSock, inPort);
	}

public:
	virtual ~RUDPSession();

protected:
	virtual void OnRecv();

private:
	SessionIdType sessionId;
	PortType port;
	SOCKET sock;
	bool isUsingSession{};
};

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

private:
	void CloseAllSessions();

private:
	bool threadStopFlag{};
	bool isServerStopped{};
	unsigned short numOfSockets{};
	PortType portStartNumber{};
	PortType sessionBrokerPort{};
	std::string ip{};

private:
	std::shared_ptr<RUDPSession> AcquireSession();
	void ReleaseSession(std::shared_ptr<RUDPSession> session);

private:
	std::unordered_map<SessionIdType, std::shared_ptr<RUDPSession>> usingSessionMap;
	std::shared_mutex usingSessionMapLock;
	std::list<std::shared_ptr<RUDPSession>> unusedSessionList;
	std::recursive_mutex unusedSessionListLock;

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
	void RunSessionBrokerThread(PortType listenPort, std::string rudpSessionIP);

private:
	std::thread sessionBrokerThread{};
#endif
};