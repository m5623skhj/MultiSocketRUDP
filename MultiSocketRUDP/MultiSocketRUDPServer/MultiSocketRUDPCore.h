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
	// @details 오류 보고 시 콜백은 해당 worker thread에서 호출되므로 StopServer()를 동기적으로 호출하지 말고,
	//          상위 레이어의 제어 스레드에 종료 또는 재시작 요청을 전달해야 합니다.
	//          오류 발생 후 등록하면 SetFatalErrorHandler() 호출 스레드에서 저장된 최초 오류를 즉시 전달합니다.
	// ----------------------------------------
	void SetFatalErrorHandler(ServerFatalErrorHandler inHandler);
	// ----------------------------------------
	// @brief 서버에서 최초로 보고된 치명 오류를 반환합니다.
	// @details 다른 스레드에서 오류를 기록하는 동안에도 안전하게 조회할 수 있습니다.
	// @return 오류가 보고되지 않았으면 std::nullopt, 아니면 저장된 최초 오류입니다.
	// ----------------------------------------
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
	// ----------------------------------------
	// @brief 최초 치명 오류를 저장하고 등록된 상위 레이어 콜백에 전달합니다.
	// @details 여러 worker thread에서 동시에 호출되어도 최초 오류만 채택합니다.
	// ----------------------------------------
	void ReportFatalError(const ServerFatalError& error);
	// ----------------------------------------
	// @brief 사용자 콜백의 예외가 worker thread 밖으로 전파되지 않도록 호출합니다.
	// ----------------------------------------
	static void NotifyFatalErrorHandler(const ServerFatalErrorHandler& handler, const ServerFatalError& error) noexcept;

private:
	// ----------------------------------------
	// @brief 완료된 수신 정보를 해당 logic worker의 큐에 전달합니다.
	// @details 큐 소유권 이전에 성공한 경우에만 true를 반환합니다.
	// ----------------------------------------
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
	// ----------------------------------------
	// @brief worker와 세션 해제 스레드가 사용할 Windows event handle을 생성합니다.
	// @return 모든 handle을 생성하면 true, 하나라도 실패하면 false입니다.
	// ----------------------------------------
	[[nodiscard]]
	bool CreateWorkerEventHandles();
	// ----------------------------------------
	// @brief worker별 완료 큐와 재전송 스케줄러를 초기화합니다.
	// @return 모든 worker 리소스가 준비되면 true입니다.
	// ----------------------------------------
	[[nodiscard]]
	bool InitializeWorkerResources();
	// ----------------------------------------
	// @brief 대기 타이머와 wake event를 소유하는 재전송 스케줄러를 생성합니다.
	// @return 생성에 실패하면 nullptr, 성공하면 초기화된 스케줄러입니다.
	// ----------------------------------------
	[[nodiscard]]
	std::unique_ptr<RetransmissionScheduler> CreateRetransmissionScheduler() const;
	// ----------------------------------------
	// @brief 준비된 리소스를 사용해 I/O, 로직, 재전송 worker thread를 시작합니다.
	// ----------------------------------------
	void StartWorkerThreads();
	// ----------------------------------------
	// @brief 세션 브로커의 listen 소켓을 초기화하고 accept thread를 시작합니다.
	// @return 브로커가 정상적으로 시작되면 true입니다.
	// ----------------------------------------
	[[nodiscard]]
	bool StartSessionBroker();

private:
	void CloseAllSessions() const;
	// ----------------------------------------
	// @brief 모든 세션의 I/O drain과 세션 풀 반환이 끝날 때까지 대기합니다.
	// @details 장시간 지연되면 진단 로그를 남기지만 반환 조건을 완화하지 않습니다.
	// ----------------------------------------
	void WaitForAllSessionsReleased() const;
	// ----------------------------------------
	// @brief 종료 대기 중인 worker thread들을 깨우도록 stop event를 신호합니다.
	// ----------------------------------------
	void SignalWorkerStopEvents() const;
	// ----------------------------------------
	// @brief worker 및 세션 해제용 event handle을 닫고 무효화합니다.
	// ----------------------------------------
	void CloseWorkerEventHandles();
	// ----------------------------------------
	// @brief 각 재전송 스케줄러가 소유한 timer와 wake event handle을 닫습니다.
	// ----------------------------------------
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
	// ----------------------------------------
	// @brief 잠금 아래 누적된 해제 대상 ID를 현재 처리 배치로 이동합니다.
	// @return 이번 반복에서 처리할 세션 ID 목록입니다.
	// ----------------------------------------
	[[nodiscard]]
	std::vector<SessionIdType> TakeReleaseSessionIds();
	// ----------------------------------------
	// @brief 세션의 I/O drain 상태를 확인하고 가능하면 최종 해제합니다.
	// @return 해제가 끝났거나 대상이 더 이상 유효하지 않으면 true입니다.
	// ----------------------------------------
	[[nodiscard]]
	bool TryFinalizeSessionRelease(SessionIdType sessionId, unsigned long long now);
	// ----------------------------------------
	// @brief 해제가 일정 시간 이상 지연된 세션의 drain 상태를 진단 로그로 남깁니다.
	// ----------------------------------------
	void LogSessionReleaseStall(RUDPSession& session, SessionIdType sessionId, unsigned long long now);
	// ----------------------------------------
	// @brief 아직 drain되지 않은 세션 ID를 다음 해제 반복에서 다시 처리하도록 등록합니다.
	// ----------------------------------------
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
	// ----------------------------------------
	// @brief 완료 큐에서 꺼낸 수신 컨텍스트 하나의 검증, 전달, 정리를 수행합니다.
	// ----------------------------------------
	void ProcessRecvIOCompletedContext(RecvIOCompletedContext* context);
	// ----------------------------------------
	// @brief 세션 generation과 해제 상태를 검증한 후 패킷 로직 실행을 시작합니다.
	// @return 패킷 처리 카운터를 획득하고 로직을 실행했으면 true입니다.
	// ----------------------------------------
	[[nodiscard]]
	bool TryDispatchRecvPacket(RecvIOCompletedContext* context);
	// ----------------------------------------
	// @brief 패킷 버퍼를 해제하고 수신 로직 카운터와 완료 컨텍스트를 정리합니다.
	// ----------------------------------------
	void CompleteRecvIOCompletedContext(RecvIOCompletedContext* context, bool processingStarted);
	// ----------------------------------------
	// @brief 지정한 logic worker의 event를 신호하고 실패 시 치명 오류로 보고합니다.
	// ----------------------------------------
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

