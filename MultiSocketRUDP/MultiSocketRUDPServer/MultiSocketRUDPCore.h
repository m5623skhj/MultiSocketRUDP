#pragma once
#include <optional>
#include <list>
#include <thread>
#include <MSWSock.h>
#include "NetServer.h"
#include <shared_mutex>
#include "RUDPSession.h"
#include "Queue.h"
#include <vector>
#include "RUDPSessionManager.h"
#include "RUDPThreadManager.h"
#include "RUDPPacketProcessor.h"
#include "RUDPIOHandler.h"
#include "IOContext.h"

#include "../Common/TLS/TLSHelper.h"

#pragma comment(lib, "ws2_32.lib")

struct SendPacketInfo;

namespace MultiSocketRUDP
{
	struct PacketSequenceSetKey
	{
		PacketSequenceSetKey(const bool inIsReplyType, const PacketSequence inPacketSequence)
			: isReplyType(inIsReplyType), packetSequence(inPacketSequence)
		{
		}

		bool operator<(const PacketSequenceSetKey& other) const
		{
			if (isReplyType != other.isReplyType)
			{
				return isReplyType < other.isReplyType;
			}

			return packetSequence < other.packetSequence;
		}

		bool isReplyType{};
		PacketSequence packetSequence{};
	};
}

class RIOManager;
class MultiSocketRUDPCoreFunctionDelegate;

class MultiSocketRUDPCore
{
	friend MultiSocketRUDPCoreFunctionDelegate;

public:
	explicit MultiSocketRUDPCore(const std::wstring& sessionBrokerCertStoreName, const std::wstring& sessionBrokerCertSubjectName);
	virtual ~MultiSocketRUDPCore() = default;

public:
	[[nodiscard]]
	bool StartServer(const std::wstring& coreOptionFilePath, const std::wstring& sessionBrokerOptionFilePath, SessionFactoryFunc&& factoryFunc, bool printLogToConsole = false);
	void StopServer();

	// ----------------------------------------
	// @brief 현재 서버에 연결된 사용자 수를 반환합니다.
	// @return 연결된 사용자 수
	// ----------------------------------------
	[[nodiscard]]
	bool IsServerStopped() const { return isServerStopped; }
	[[nodiscard]]
	unsigned short GetConnectedUserCount() const;

public:
	bool SendPacket(SendPacketInfo* sendPacketInfo, bool needAddRefCount = true) const;
	void EraseSendPacketInfo(OUT SendPacketInfo* eraseTarget, ThreadIdType threadId);
	RIO_EXTENSION_FUNCTION_TABLE GetRIOFunctionTable() const { return rioManager->GetRIOFunctionTable(); }

	// Never call this function directly. It should only be called within RDPSession::Disconnect()
	void DisconnectSession(SessionIdType disconnectTargetSessionId) const;
	void PushToDisconnectTargetSession(RUDPSession& session);

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

private:
	void EnqueueContextResult(IOContext* contextResult, BYTE threadId);

private:
	[[nodiscard]]
	bool ReadOptionFile(const std::wstring& coreOptionFilePath, const std::wstring& sessionBrokerOptionFilePath);
	[[nodiscard]]
	bool InitNetwork() const;
	[[nodiscard]]
	bool InitRIO();
	[[nodiscard]]
	bool RunAllThreads();
	[[nodiscard]]
	bool RunSessionBroker();
	[[nodiscard]]
	static SOCKET CreateRUDPSocket();

private:
	void CloseAllSessions() const;
	void ClearAllSession();
	void ReleaseAllSession() const;

private:
	bool threadStopFlag{};
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
	std::unique_ptr<RUDPSessionManager> sessionManager;

private:
	std::vector<std::list<SendPacketInfo*>> sendPacketInfoList;
	std::vector<std::unique_ptr<std::mutex>> sendPacketInfoListLock;

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
	void RunSessionBrokerThread(PortType listenPort, const std::string& rudpSessionIP);
	[[nodiscard]]
	bool OpenSessionBrokerSocket(PortType listenPort);
	[[nodiscard]]
	static bool InitSessionCrypto(OUT RUDPSession& session);
	[[nodiscard]]
	static bool GenerateSessionKey(OUT RUDPSession& session);
	[[nodiscard]]
	static bool GenerateSaltKey(OUT RUDPSession& session);
	static void SetSessionInfoToBuffer(const RUDPSession& session, const std::string& rudpSessionIP, OUT NetBuffer& buffer);
	[[nodiscard]]
	RUDPSession* ReserveSession(OUT NetBuffer& sendBuffer, const std::string& rudpSessionIP);
	[[nodiscard]]
	CONNECT_RESULT_CODE InitReserveSession(RUDPSession& session) const;
	[[nodiscard]]
	bool SendSessionInfoToClient(const SOCKET& clientSocket, OUT NetBuffer& sendBuffer);
	static bool SendAll(const SOCKET& socket, const char* sendBuffer, const size_t sendSize);

private:
	std::jthread sessionBrokerThread{};
	SOCKET sessionBrokerListenSocket{ INVALID_SOCKET };
#endif

	TLSHelper::TLSHelperServer tlsHelper;

private:
	void StopAllThreads() const;
	void RunIOWorkerThread(const std::stop_token& stopToken, ThreadIdType threadId);
	void RunRecvLogicWorkerThread(const std::stop_token& stopToken, ThreadIdType threadId);
	void RunRetransmissionThread(const std::stop_token& stopToken, ThreadIdType threadId);
	void RunSessionReleaseThread(const std::stop_token& stopToken);
	void RunHeartbeatThread(const std::stop_token& stopToken) const;
	FORCEINLINE static void SleepRemainingFrameTime(OUT TickSet& tickSet, unsigned int intervalMs);

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
	std::vector<CListBaseQueue<IOContext*>> ioCompletedContexts;
	std::list<SessionIdType> releaseSessionIdList;
	std::mutex releaseSessionIdListLock;

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
};

static auto sendPacketInfoPool = new CTLSMemoryPool<SendPacketInfo>(2, true);

struct SendPacketInfo
{
	NetBuffer* buffer{};
	RUDPSession* owner{};
	PacketRetransmissionCount retransmissionCount{};
	PacketSequence sendPacketSequence{};
	unsigned long long retransmissionTimeStamp{};
	bool isErasedPacketInfo{};
	bool isReplyType{};
	std::list<SendPacketInfo*>::iterator listItor;
	std::atomic_int8_t refCount{};

	~SendPacketInfo()
	{
		owner = {};
		retransmissionCount = {};
		sendPacketSequence = {};
		retransmissionTimeStamp = {};
		listItor = {};
		isErasedPacketInfo = {};
		isReplyType = {};
	}

	void Initialize(RUDPSession* inOwner, NetBuffer* inBuffer, const PacketSequence inSendPacketSequence, const bool inIsReplyType)
	{
		owner = inOwner;
		buffer = inBuffer;
		sendPacketSequence = inSendPacketSequence;
		isReplyType = inIsReplyType;

		retransmissionCount = {};
		retransmissionTimeStamp = {};
		isErasedPacketInfo = {};

		refCount = 1;
	}

	void AddRefCount()
	{
		refCount.fetch_add(1, std::memory_order_relaxed);
	}

	static void Free(SendPacketInfo* deleteTarget)
	{
		if (deleteTarget == nullptr)
		{
			return;
		}

		if (deleteTarget->refCount.fetch_sub(1, std::memory_order_relaxed) == 1)
		{
			NetBuffer::Free(deleteTarget->buffer);
			sendPacketInfoPool->Free(deleteTarget);
		}
	}
	
	static void Free(SendPacketInfo* deleteTarget, const char subCount)
	{
		if (deleteTarget == nullptr)
		{
			return;
		}

		if (deleteTarget->refCount.fetch_sub(subCount, std::memory_order_relaxed) == 1)
		{
			NetBuffer::Free(deleteTarget->buffer);
			sendPacketInfoPool->Free(deleteTarget);
		}
	}

	[[nodiscard]]
	NetBuffer* GetBuffer() const { return buffer; }
};