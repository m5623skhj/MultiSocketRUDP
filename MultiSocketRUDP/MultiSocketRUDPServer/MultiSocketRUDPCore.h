#pragma once
#include "IMultiSocketRUDPCore.h"
#include "RetransmissionScheduler.h"
#include <list>
#include <memory>
#include <thread>
#include <MSWSock.h>
#include "../Common/TLS/TLSHelper.h"
#include "RUDPSession.h"
#include <queue>
#include <vector>
#include "RUDPSessionFunctionDelegate.h"
#include "IOContext.h"
#include "RUDPSessionManager.h"
#include <functional>
#include <mutex>
#include <optional>

#pragma comment(lib, "ws2_32.lib")

struct SendPacketInfo;

class RIOManager;
class MultiSocketRUDPCoreFunctionDelegate;
class RUDPThreadManager;
class RUDPPacketProcessor;
class RUDPIOHandler;
class RUDPSessionBroker;
class RUDPSessionManager;
class MultiSocketRUDPCoreTestAccess;

enum class SERVER_FATAL_ERROR_CODE : unsigned char
{
	RIO_COMPLETION_QUEUE_CORRUPT,
	RECV_LOGIC_WAIT_FAILED,
	RECV_LOGIC_EVENT_SIGNAL_FAILED,
};

struct ServerFatalError
{
	SERVER_FATAL_ERROR_CODE code{};
	ThreadIdType threadId{};
	unsigned long nativeErrorCode{};
};

using ServerFatalErrorHandler = std::function<void(const ServerFatalError&)>;

class MultiSocketRUDPCore : public ICore
{
	friend MultiSocketRUDPCoreFunctionDelegate;
	friend MultiSocketRUDPCoreTestAccess;

private:
	// Receives are produced by an I/O worker and consumed by its paired logic worker.
	// Owning synchronization here also gives the queue a deterministic, cycle-free teardown.
	class RecvIOCompletedQueue final
	{
	public:
		void Enqueue(RecvIOCompletedContext* inContext);
		[[nodiscard]]
		bool Dequeue(RecvIOCompletedContext** outContext);

	private:
		std::mutex queueLock;
		std::queue<RecvIOCompletedContext*> contexts;
	};

public:
	explicit MultiSocketRUDPCore(std::wstring&& inSessionBrokerCertStoreName, std::wstring&& inSessionBrokerCertSubjectName);
	explicit MultiSocketRUDPCore(TLSHelper::ServerCertificateConfig inSessionBrokerCertificateConfig);
	~MultiSocketRUDPCore() override;

public:
	[[nodiscard]]
	bool StartServer(const std::wstring& coreOptionFilePath, const std::wstring& sessionBrokerOptionFilePath, SessionFactoryFunc&& factoryFunc, bool printLogToConsole = false);
	void StopServer();

	// ----------------------------------------
	// @brief 서버가 안전하게 계속 실행될 수 없는 오류를 상위 레이어에 전달할 콜백을 설정합니다.
	// @details 콜백은 오류가 발생한 worker thread에서 호출되므로 StopServer()를 동기적으로 호출하지 말고,
	//          상위 레이어의 제어 스레드에 종료 또는 재시작 요청을 전달해야 합니다.
	//          오류 발생 후 콜백을 설정해도 저장된 최초 오류를 즉시 전달합니다.
	// ----------------------------------------
	void SetFatalErrorHandler(ServerFatalErrorHandler inHandler);
	[[nodiscard]]
	std::optional<ServerFatalError> GetFatalError() const;

	// ----------------------------------------
	// @brief 현재 서버에 연결된 사용자 수를 반환합니다.
	// @return 연결된 사용자 수
	// ----------------------------------------
	[[nodiscard]]
	bool IsServerStopped() const;
	[[nodiscard]]
	unsigned short GetNowSessionCount() const;
	[[nodiscard]]
	unsigned short GetUnusedSessionCount() const;
	[[nodiscard]]
	unsigned int GetAllConnectedCount() const;
	[[nodiscard]]
	unsigned int GetAllDisconnectedCount() const;
	[[nodiscard]]
	unsigned int GetAllDisconnectedByRetransmissionCount() const;

public:
	bool SendPacket(SendPacketInfo* sendPacketInfo) const override;
	void MarkSendPacketInfoErased(OUT SendPacketInfo* eraseTarget, ThreadIdType threadId) override;
	RIO_EXTENSION_FUNCTION_TABLE GetRIOFunctionTable() const override;

	// ----------------------------------------
	// @brief NetBuffer에서 페이로드 길이를 추출합니다.
	// @param buffer 페이로드 길이를 포함하는 NetBuffer 객체
	// @return 페이로드 길이 (WORD 형식)
	// ----------------------------------------
	[[nodiscard]]
	static WORD GetPayloadLength(OUT const NetBuffer& buffer)
	{
		static constexpr int PAYLOAD_LENGTH_POSITION = 1;
		return *reinterpret_cast<WORD*>(&buffer.m_pSerializeBuffer[PAYLOAD_LENGTH_POSITION]);
	}

	int32_t GetTPS() const;
	void ResetTPS() const;
	unsigned int GetHeartbeatThreadSleepMs() const;
	// ----------------------------------------
	// @brief Returns initial RTO used before any valid RTT sample exists.
	// ----------------------------------------
	unsigned int GetInitialRetransmissionMs() const;
	// ----------------------------------------
	// @brief Returns lower bound for dynamic retransmission timeout.
	// ----------------------------------------
	unsigned int GetMinRetransmissionMs() const;
	// ----------------------------------------
	// @brief Returns upper bound for dynamic retransmission timeout and backoff.
	// ----------------------------------------
	unsigned int GetMaxRetransmissionMs() const;

private:
	void DisconnectSession(SessionIdType disconnectTargetSessionId) const override;
	void PushToDisconnectTargetSession(RUDPSession& session) override;
	void StopLoggerThread();
	void ReportFatalError(const ServerFatalError& error);
	static void NotifyFatalErrorHandler(const ServerFatalErrorHandler& handler, const ServerFatalError& error) noexcept;

private:
	[[nodiscard]]
	bool EnqueueContextResult(const IOContext* contextResult, NetBuffer* buffer, BYTE threadId);

private:
	[[nodiscard]]
	bool ReadOptionFile(const std::wstring& coreOptionFilePath, const std::wstring& sessionBrokerOptionFilePath);
	[[nodiscard]]
	static BYTE GetPacketHeaderCodeForTest() noexcept { return NetBuffer::m_byHeaderCode; }
	[[nodiscard]]
	static BYTE GetPacketXorCodeForTest() noexcept { return NetBuffer::m_byXORCode; }
	static void SetPacketCodesForTest(const BYTE headerCode, const BYTE xorCode) noexcept
	{
		NetBuffer::m_byHeaderCode = headerCode;
		NetBuffer::m_byXORCode = xorCode;
	}
	[[nodiscard]]
	bool InitNetwork() const;
	[[nodiscard]]
	bool InitRIO();
	[[nodiscard]]
	bool RunAllThreads();
	[[nodiscard]]
	bool CreateWorkerEventHandles();
	[[nodiscard]]
	bool InitializeWorkerResources();
	[[nodiscard]]
	std::unique_ptr<RetransmissionScheduler> CreateRetransmissionScheduler() const;
	void StartWorkerThreads();
	[[nodiscard]]
	bool StartSessionBroker();

private:
	void CloseAllSessions() const;
	void WaitForAllSessionsReleased() const;
	void SignalWorkerStopEvents() const;
	void CloseWorkerEventHandles();
	void CloseRetransmissionSchedulerHandles();
	void ClearAllSession();
	void ReleaseAllSession() const;

private:
	bool isServerStopped{};
	mutable std::mutex fatalErrorLock;
	ServerFatalErrorHandler fatalErrorHandler;
	std::optional<ServerFatalError> fatalError;
	unsigned short numOfSockets{};
	PortType sessionBrokerPort{};
	std::string coreServerIp{};

private:
	[[nodiscard]]
	RUDPSession* AcquireSession() const;
	[[nodiscard]]
	inline RUDPSession* GetUsingSession(SessionIdType sessionId) const;
	RUDPSession* GetReleasingSession(SessionIdType sessionId) const;

private:
	std::vector<std::unique_ptr<RetransmissionScheduler>> retransmissionSchedulers;

private:
	[[nodiscard]]
	CONNECT_RESULT_CODE InitReserveSession(OUT RUDPSession& session) const;

private:
	TLSHelper::ServerCertificateConfig sessionBrokerCertificateConfig{};

#pragma region thread
private:
	void StopAllThreads() const;
	void RunIOWorkerThread(const std::stop_token& stopToken, ThreadIdType threadId);
	void RunRecvLogicWorkerThread(const std::stop_token& stopToken, ThreadIdType threadId);
	void RunRetransmissionThread(const std::stop_token& stopToken, ThreadIdType threadId);
	void ProcessRetransmission(SendPacketInfo* sendPacketInfo, ThreadIdType threadId);
	void RunSessionReleaseThread(const std::stop_token& stopToken);
	[[nodiscard]]
	std::vector<SessionIdType> TakeReleaseSessionIds();
	[[nodiscard]]
	bool TryFinalizeSessionRelease(SessionIdType sessionId, unsigned long long now);
	void LogSessionReleaseStall(RUDPSession& session, SessionIdType sessionId, unsigned long long now);
	void RequeueReleaseSessionIds(const std::vector<SessionIdType>& sessionIds);
	void RunHeartbeatThread(const std::stop_token& stopToken) const;

private:
	unsigned char numOfWorkerThread{};
	PacketRetransmissionCount maxPacketRetransmissionCount{};
	unsigned int workerThreadOneFrameMs{};
	unsigned int retransmissionMs{};
	unsigned int minRetransmissionMs{};
	unsigned int maxRetransmissionMs{};
	unsigned int heartbeatThreadSleepMs{};
	unsigned int timerTickMs{};
	BYTE maxHoldingPacketQueueSize{};
	unsigned int simulatedPacketLossPercent{};
	int simulatedPacketLossSeed{};

	std::unique_ptr<RUDPThreadManager> threadManager;

	// event handles
	HANDLE recvLogicThreadEventStopHandle{};
	std::vector<HANDLE> recvLogicThreadEventHandles;
	HANDLE sessionReleaseStopEventHandle{};
	HANDLE sessionReleaseEventHandle{};
	HANDLE retransmissionStopEventHandle{};

	// objects
	std::vector<std::unique_ptr<RecvIOCompletedQueue>> recvIOCompletedContexts;
	std::list<SessionIdType> releaseSessionIdList;
	std::mutex releaseSessionIdListLock;
	CTLSMemoryPool<RecvIOCompletedContext> recvIOCompletedContextPool;

#pragma endregion thread

private:
	[[nodiscard]]
	void OnRecvPacket(BYTE threadId);
	void ProcessRecvIOCompletedContext(RecvIOCompletedContext* context);
	[[nodiscard]]
	bool TryDispatchRecvPacket(RecvIOCompletedContext* context);
	void CompleteRecvIOCompletedContext(RecvIOCompletedContext* context, bool processingStarted);
	void SignalRecvLogicThread(BYTE threadId);

private:
	// Dependencies below retain references to this delegate, so it must outlive them.
	RUDPSessionFunctionDelegate sessionDelegate;
	CTLSMemoryPool<IOContext> contextPool;
	std::unique_ptr<RIOManager> rioManager;
	std::unique_ptr<RUDPPacketProcessor> packetProcessor;
	std::unique_ptr<RUDPIOHandler> ioHandler;
	std::unique_ptr<RUDPSessionBroker> sessionBroker;
	std::unique_ptr<RUDPSessionManager> sessionManager;
};

