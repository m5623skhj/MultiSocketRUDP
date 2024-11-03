#pragma once
#include <optional>
#include <list>
#include <thread>
#include <MSWSock.h>
#include "NetServer.h"
#include "NetServerSerializeBuffer.h"
#include <unordered_map>
#include <shared_mutex>
#include "RUDPSession.h"

#pragma comment(lib, "ws2_32.lib")

struct TickSet
{
	UINT64 nowTick = 0;
	UINT64 beforeTick = 0;
};

struct IOContext : RIO_BUF
{
	IOContext() = default;
	~IOContext() = default;

	void InitContext(SessionIdType inOwnerSessionId, RIO_OPERATION_TYPE inIOType);

	SessionIdType ownerSessionId = invalidSessionId;
	sockaddr_in clientAddr{};
	RIO_OPERATION_TYPE ioType = RIO_OPERATION_TYPE::OP_ERROR;
};

struct IOContextResult
{
	IOContext* ioContext;
	std::shared_ptr<RUDPSession> session;
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
	bool InitRIO();
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
	std::shared_ptr<RUDPSession> GetUsingSession(SessionIdType sessionId);
	void ReleaseSession(std::shared_ptr<RUDPSession> session);

private:
	std::unordered_map<SessionIdType, std::shared_ptr<RUDPSession>> usingSessionMap;
	std::shared_mutex usingSessionMapLock;
	std::list<std::shared_ptr<RUDPSession>> unusedSessionList;
	std::recursive_mutex unusedSessionListLock;

#pragma region thread
#if USE_IOCP_SESSION_BROKER
private:
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
private:
	void RunSessionBrokerThread(PortType listenPort, std::string rudpSessionIP);
	void SetSessionKey(OUT std::shared_ptr<RUDPSession> session);
	void SetSessionInfoToBuffer(std::shared_ptr<RUDPSession> session, const std::string& rudpSessionIP, OUT NetBuffer& buffer);

private:
	std::thread sessionBrokerThread{};
#endif

private:
	void RunWorkerThread(ThreadIdType threadId);
	FORCEINLINE void SleepRemainingFrameTime(OUT TickSet& tickSet);

private:
	ThreadIdType numOfWorkerThread{};
	std::vector<std::thread> workerThreads;

#pragma endregion thread

#pragma region RIO
private:
	std::optional<IOContextResult> GetIOCompletedContext(RIORESULT& rioResult);
	bool IOCompleted(IOContext& context, ULONG transferred, RUDPSession& session, BYTE threadId);

	bool RecvIOCompleted(ULONG transferred, RUDPSession& session, BYTE threadId);
	bool SendIOCompleted(ULONG transferred, RUDPSession& session, BYTE threadId);

private:
	RIO_EXTENSION_FUNCTION_TABLE rioFunctionTable{};
	RIO_CQ* rioCQList = nullptr;

private:
	CTLSMemoryPool<IOContext> contextPool;
#pragma endregion RIO
};