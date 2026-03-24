#pragma once
#include "IMultiSocketRUDPCore.h"
#include <list>
#include <thread>
#include <MSWSock.h>
#include "RUDPSession.h"
#include "Queue.h"
#include <vector>
#include "RUDPSessionFunctionDelegate.h"
#include "IOContext.h"
#include "RUDPSessionManager.h"

#pragma comment(lib, "ws2_32.lib")

struct SendPacketInfo;

class RIOManager;
class MultiSocketRUDPCoreFunctionDelegate;
class RUDPThreadManager;
class RUDPPacketProcessor;
class RUDPIOHandler;
class RUDPSessionBroker;
class RUDPSessionManager;

class MultiSocketRUDPCore : public ICore
{
	friend MultiSocketRUDPCoreFunctionDelegate;

public:
	explicit MultiSocketRUDPCore(std::wstring&& inSessionBrokerCertStoreName, std::wstring&& inSessionBrokerCertSubjectName);
	~MultiSocketRUDPCore() override;

public:
	[[nodiscard]]
	bool StartServer(const std::wstring& coreOptionFilePath, const std::wstring& sessionBrokerOptionFilePath, SessionFactoryFunc&& factoryFunc, bool printLogToConsole = false);
	void StopServer();

	// ----------------------------------------
	// @brief 현재 서버에 연결된 사용자 수를 반환합니다.
	// @return 연결된 사용자 수
	// ----------------------------------------
	[[nodiscard]]
	bool IsServerStopped() const;
	[[nodiscard]]
	unsigned short GetNowSessionCount() const;
	[[nodiscard]]
	unsigned int GetAllConnectedCount() const;
	[[nodiscard]]
	unsigned int GetAllDisconnectedCount() const;
	[[nodiscard]]
	unsigned int GetAllDisconnectedByRetransmissionCount() const;

public:
	bool SendPacket(SendPacketInfo* sendPacketInfo) const override;
	void EraseSendPacketInfo(OUT SendPacketInfo* eraseTarget, ThreadIdType threadId) override;
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

private:
	void DisconnectSession(SessionIdType disconnectTargetSessionId) const override;
	void PushToDisconnectTargetSession(RUDPSession& session) override;

private:
	void EnqueueContextResult(const IOContext* contextResult, BYTE threadId);

private:
	[[nodiscard]]
	bool ReadOptionFile(const std::wstring& coreOptionFilePath, const std::wstring& sessionBrokerOptionFilePath);
	[[nodiscard]]
	bool InitNetwork() const;
	[[nodiscard]]
	bool InitRIO();
	[[nodiscard]]
	bool RunAllThreads();

private:
	void CloseAllSessions() const;
	void ClearAllSession();
	void ReleaseAllSession() const;

private:
	bool isServerStopped{};
	unsigned short numOfSockets{};
	PortType sessionBrokerPort{};
	std::string coreServerIp{};

private:
	[[nodiscard]]
	RUDPSession* AcquireSession() const;
	[[nodiscard]]
	inline RUDPSession* GetUsingSession(SessionIdType sessionId) const;
	inline RUDPSession* GetReleasingSession(SessionIdType sessionId) const;

private:
	std::vector<std::list<SendPacketInfo*>> sendPacketInfoList;
	std::vector<std::unique_ptr<std::mutex>> sendPacketInfoListLock;

private:
	[[nodiscard]]
	CONNECT_RESULT_CODE InitReserveSession(OUT RUDPSession& session) const;

private:
	std::wstring sessionBrokerCertStoreName{};
	std::wstring sessionBrokerCertSubjectName{};

#pragma region thread
private:
	void StopAllThreads() const;
	void RunIOWorkerThread(const std::stop_token& stopToken, ThreadIdType threadId);
	void RunRecvLogicWorkerThread(const std::stop_token& stopToken, ThreadIdType threadId);
	void RunRetransmissionThread(const std::stop_token& stopToken, ThreadIdType threadId);
	void RunSessionReleaseThread(const std::stop_token& stopToken);
	void RunHeartbeatThread(const std::stop_token& stopToken) const;

private:
	unsigned char numOfWorkerThread{};
	PacketRetransmissionCount maxPacketRetransmissionCount{};
	unsigned int workerThreadOneFrameMs{};
	unsigned int retransmissionMs{};
	unsigned int retransmissionThreadSleepMs{};
	unsigned int heartbeatThreadSleepMs{};
	unsigned int timerTickMs{};
	BYTE maxHoldingPacketQueueSize{};

	std::unique_ptr<RUDPThreadManager> threadManager;

	// event handles
	HANDLE recvLogicThreadEventStopHandle{};
	std::vector<HANDLE> recvLogicThreadEventHandles;
	HANDLE sessionReleaseStopEventHandle{};
	HANDLE sessionReleaseEventHandle{};

	// objects
	std::vector<CListBaseQueue<RecvIOCompletedContext*>> recvIOCompletedContexts;
	std::list<SessionIdType> releaseSessionIdList;
	std::mutex releaseSessionIdListLock;
	CTLSMemoryPool<RecvIOCompletedContext> recvIOCompletedContextPool;

#pragma endregion thread

private:
	[[nodiscard]]
	IOContext* GetIOCompletedContext(const RIORESULT& rioResult);
	void OnRecvPacket(BYTE threadId);

private:
	CTLSMemoryPool<IOContext> contextPool;
	std::unique_ptr<RIOManager> rioManager;
	std::unique_ptr<RUDPPacketProcessor> packetProcessor;
	std::unique_ptr<RUDPIOHandler> ioHandler;
	std::unique_ptr<RUDPSessionBroker> sessionBroker;
	std::unique_ptr<RUDPSessionManager> sessionManager;
	RUDPSessionFunctionDelegate sessionDelegate;
};