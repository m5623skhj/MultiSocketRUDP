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
#include "Queue.h"
#include <vector>

#pragma comment(lib, "ws2_32.lib")

struct IOContext : RIO_BUF
{
	IOContext() = default;
	~IOContext() = default;

	inline void InitContext(SessionIdType inOwnerSessionId, RIO_OPERATION_TYPE inIOType);

	SessionIdType ownerSessionId = invalidSessionId;
	RIO_OPERATION_TYPE ioType = RIO_OPERATION_TYPE::OP_ERROR;
	RUDPSession* session = nullptr;
	RIO_BUF clientAddrRIOBuffer{ RIO_INVALID_BUFFERID };
	RIO_BUF localAddrRIOBuffer{ RIO_INVALID_BUFFERID };
	char clientAddrBuffer[sizeof(SOCKADDR_INET)];
	char localAddrBuffer[sizeof(SOCKADDR_INET)];
};

struct SendPacketInfo
{
	NetBuffer* buffer{};
	RUDPSession* owner{};
	PacketRetransmissionCount retransmissionCount{};
	PacketSequence sendPacektSequence{};
	unsigned long long sendTimeStamp{};
	std::list<SendPacketInfo*>::iterator listItor;

	~SendPacketInfo()
	{
		NetBuffer::Free(buffer);
		owner = {};
		retransmissionCount = {};
		sendPacektSequence = {};
		sendTimeStamp = {};
		listItor = {};
	}

	inline void Initialize(RUDPSession* inOwner, NetBuffer* inBuffer, const PacketSequence inSendPacketSequence)
	{
		owner = inOwner;
		buffer = inBuffer;
		sendPacektSequence = inSendPacketSequence;
		NetBuffer::AddRefCount(inBuffer);
	}

	[[nodiscard]]
	inline NetBuffer* GetBuffer() { return buffer; }
};

class MultiSocketRUDPCore
{
public:
	MultiSocketRUDPCore();
	virtual ~MultiSocketRUDPCore() = default;

public:
	[[nodiscard]]
	bool StartServer(const std::wstring& coreOptionFilePath, const std::wstring& sessionBrokerOptionFilePath, bool printLogToConsole = false);
	void StopServer();

	[[nodiscard]]
	inline bool IsServerStopped() const { return isServerStopped; }
	[[nodiscard]]
	inline unsigned short GetConnectedUserCount() const { return connectedUserCount; }

private:
	inline void StopThread(std::thread& stopTarget, const std::thread::id& threadId);

public:
	bool SendPacket(SendPacketInfo* sendPacketInfo);
	[[nodiscard]]
	bool DoSend(OUT RUDPSession& session, const ThreadIdType threadId);
	[[nodiscard]]
	IOContext* MakeSendContext(OUT RUDPSession& session, const ThreadIdType threadId);
	[[nodiscard]]
	bool TryRIOSend(OUT RUDPSession& session, IOContext* context);
	// Never call this function directly. It should only be called within RDPSession::Disconnect()
	void DisconnectSession(const SessionIdType disconnectTargetSessionId);
	void EraseSendPacketInfo(OUT SendPacketInfo* eraseTarget, const ThreadIdType threadId);
	inline void PushToDisconnectTargetSession(SessionIdType disconnectTargetSessionId);

private:
	[[nodiscard]]
	bool ReadOptionFile(const std::wstring& coreOptionFilePath, const std::wstring& sessionBrokerOptionFilePath);
	[[nodiscard]]
	bool InitNetwork();
	[[nodiscard]]
	bool InitRIO();
	[[nodiscard]]
	bool InitSessionRecvBuffer(RUDPSession* session);
	[[nodiscard]]
	inline RIO_BUFFERID RegisterRIOBuffer(char* targetBuffer, const unsigned int targetBuffersize) const;
	[[nodiscard]]
	bool RunAllThreads();
	[[nodiscard]]
	bool RunSessionBroker();
	[[nodiscard]]
	std::optional<SOCKET> CreateRUDPSocket(const unsigned short socketNumber) const;

private:
	void CloseAllSessions();

private:
	bool threadStopFlag{};
	bool isServerStopped{};
	unsigned short numOfSockets{};
	PortType portStartNumber{};
	PortType sessionBrokerPort{};
	std::string coreServerIp{};

private:
	[[nodiscard]]
	RUDPSession* AcquireSession();
	[[nodiscard]]
	inline RUDPSession* GetUsingSession(const SessionIdType sessionId) const;

private:
	// This container's size must not be increased any further
	std::vector<RUDPSession*> sessionArray;
	std::list<SessionIdType> unusedSessionIdList;
	std::recursive_mutex unusedSessionIdListLock;
	std::atomic_uint16_t connectedUserCount{};

private:
	std::vector<std::list<SendPacketInfo*>> sendedPacketInfoList;
	std::vector<std::unique_ptr<std::mutex>> sendedPacketInfoListLock;

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
	void RunSessionBrokerThread(const PortType listenPort, const std::string& rudpSessionIP);
	[[nodiscard]]
	bool OpenSessionBrokerSocket(const PortType listenPort, OUT SOCKET& listenSocket);
	void SetSessionKey(OUT RUDPSession& session);
	void SetSessionInfoToBuffer(RUDPSession& session, const std::string& rudpSessionIP, OUT NetBuffer& buffer);
	void ReserveSession(OUT NetBuffer& sendBuffer, const std::string& rudpSessionIP);
	void SendSessionInfoToClient(OUT SOCKET& clientSocket, OUT NetBuffer& sendBuffer);

private:
	std::thread sessionBrokerThread{};
#endif

private:
	void RunWorkerThread(const ThreadIdType threadId);
	void RunRecvLogicWorkerThread(const ThreadIdType threadId);
	void RunRetransmissionThread(const ThreadIdType threadId);
	void RunSessionReleaseThread();
	FORCEINLINE void SleepRemainingFrameTime(OUT TickSet& tickSet, const unsigned int intervalMs);

private:
	unsigned char numOfWorkerThread{};
	PacketRetransmissionCount maxPacketRetransmissionCount{};
	unsigned int workerThreadOneFrameMs{};
	unsigned int retransmissionMs{};
	unsigned int retransmissionThreadSleepMs{};

	// threads
	std::vector<std::thread> ioWorkerThreads;
	std::vector<std::thread> recvLogicWorkerThreads;
	std::vector<std::thread> retransmissionThreads;
	std::thread sessionReleaseThread{};

	// event handles
	HANDLE logicThreadEventStopHandle{};
	std::vector<HANDLE> recvLogicThreadEventHandles;
	HANDLE sessionReleaseEventHandle{};

	// objects
	std::vector<CListBaseQueue<IOContext*>> ioCompletedContexts;
	std::list<SessionIdType> releaseSessionIdList;
	std::mutex releaseSessionIdListLock;

#pragma endregion thread

#pragma region RIO
private:
	[[nodiscard]]
	IOContext* GetIOCompletedContext(RIORESULT& rioResult);
	[[nodiscard]]
	bool IOCompleted(OUT IOContext* contextResult, const ULONG transferred, const BYTE threadId);
	[[nodiscard]]
	bool RecvIOCompleted(OUT IOContext* contextResult, const ULONG transferred, const BYTE threadId);
	[[nodiscard]]
	inline bool SendIOCompleted(RUDPSession& session, const BYTE threadId);

	void OnRecvPacket(const BYTE threadId);
	[[nodiscard]]
	bool ProcessByPacketType(RUDPSession& session, const sockaddr_in& clientAddr, NetBuffer& recvPacket);
	[[nodiscard]]
	bool DoRecv(RUDPSession& session);
	[[nodiscard]]
	int MakeSendStream(OUT RUDPSession& session, OUT IOContext* context, const ThreadIdType threadId);

private:
	RIO_EXTENSION_FUNCTION_TABLE rioFunctionTable{};
	RIO_CQ* rioCQList = nullptr;

private:
	CTLSMemoryPool<IOContext> contextPool;
#pragma endregion RIO

private:
	void EncodePacket(OUT NetBuffer& packet);
	[[nodiscard]]
	inline WORD GetPayloadLength(OUT NetBuffer& buffer) const;
};

static CTLSMemoryPool<SendPacketInfo>* sendPacketInfoPool = new CTLSMemoryPool<SendPacketInfo>(2, true);
