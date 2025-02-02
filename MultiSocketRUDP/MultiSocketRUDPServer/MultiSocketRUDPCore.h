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

	void InitContext(SessionIdType inOwnerSessionId, RIO_OPERATION_TYPE inIOType);

	SessionIdType ownerSessionId = invalidSessionId;
	RIO_OPERATION_TYPE ioType = RIO_OPERATION_TYPE::OP_ERROR;
	RUDPSession* session = nullptr;
	RIO_BUFFERID clientAddrBufferId{ RIO_INVALID_BUFFERID };
	char clientAddrBuffer[sizeof(sockaddr_in)];
};

struct SendPacketInfo
{
	NetBuffer* buffer{};
	RUDPSession* owner{};
	PacketRetransmissionCount retransmissionCount{};
	PacketSequence sendPacektSequence{};
	unsigned long long sendTimeStamp{};
	std::list<SendPacketInfo*>::iterator listItor;

	void Initialize(RUDPSession* inOwner, NetBuffer* inBuffer, const PacketSequence inSendPacketSequence)
	{
		owner = inOwner;
		buffer = inBuffer;
		sendPacektSequence = inSendPacketSequence;
		NetBuffer::AddRefCount(inBuffer);
	}

	[[nodiscard]]
	NetBuffer* GetBuffer() { return buffer; }
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
	bool IsServerStopped();

public:
	bool SendPacket(SendPacketInfo* sendPacketInfo);
	bool DoSend(OUT RUDPSession& session, ThreadIdType threadId);
	// Never call this function directly. It should only be called within RDPSession::Disconnect()
	void DisconnectSession(const SessionIdType disconnectTargetSessionId);
	void EraseSendPacketInfo(OUT SendPacketInfo* eraseTarget, ThreadIdType threadId);

private:
	[[nodiscard]]
	bool ReadOptionFile(const std::wstring& coreOptionFilePath, const std::wstring& sessionBrokerOptionFilePath);
	[[nodiscard]]
	bool InitNetwork();
	[[nodiscard]]
	bool InitRIO();
	[[nodiscard]]
	inline RIO_BUFFERID RegisterRIOBuffer(char* targetBuffer, unsigned int targetBuffersize);
	[[nodiscard]]
	bool RunAllThreads();
	[[nodiscard]]
	bool RunSessionBroker();
	[[nodiscard]]
	std::optional<SOCKET> CreateRUDPSocket(unsigned short socketNumber);

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
	RUDPSession* AcquireSession();
	RUDPSession* GetUsingSession(SessionIdType sessionId);

private:
	// This container's size must not be increased any further
	std::vector<RUDPSession*> sessionArray;
	std::list<SessionIdType> unusedSessionIdList;
	std::recursive_mutex unusedSessionIdListLock;

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
	void RunSessionBrokerThread(PortType listenPort, std::string rudpSessionIP);
	void SetSessionKey(OUT RUDPSession& session);
	void SetSessionInfoToBuffer(RUDPSession& session, const std::string& rudpSessionIP, OUT NetBuffer& buffer);

private:
	std::thread sessionBrokerThread{};
#endif

private:
	void RunWorkerThread(ThreadIdType threadId);
	void RunRecvLogicWorkerThread(ThreadIdType threadId);
	void RunRetransmissionThread(ThreadIdType threadId);
	void RunTimeoutThread();
	FORCEINLINE void SleepRemainingFrameTime(OUT TickSet& tickSet, unsigned int intervalMs);

private:
	unsigned char numOfWorkerThread{};
	HANDLE logicThreadEventStopHandle{};
	PacketRetransmissionCount maxPacketRetransmissionCount{};
	unsigned int workerThreadOneFrameMs{};
	unsigned int retransmissionMs{};
	unsigned int retransmissionThreadSleepMs{};

	// threads
	std::vector<std::thread> ioWorkerThreads;
	std::vector<std::thread> recvLogicWorkerThreads;
	std::vector<std::thread> retransmissionThreads;
	std::thread timeoutThread{};

	// event handles
	std::vector<HANDLE> recvLogicThreadEventHandles;
	HANDLE timeoutEventHandle{};

	// objects
	std::vector<CListBaseQueue<IOContext*>> ioCompletedContexts;
	std::list<RUDPSession*> timeoutSessionList;
	std::mutex timeoutSessionListLock;

#pragma endregion thread

#pragma region RIO
private:
	IOContext* GetIOCompletedContext(RIORESULT& rioResult);
	bool IOCompleted(OUT IOContext* contextResult, ULONG transferred, BYTE threadId);
	bool RecvIOCompleted(OUT IOContext* contextResult, ULONG transferred, BYTE threadId);
	bool SendIOCompleted(ULONG transferred, RUDPSession& session, BYTE threadId);

	void OnRecvPacket(BYTE threadId);
	bool ProcessByPacketType(RUDPSession& session, const sockaddr_in& clientAddr, NetBuffer& recvPacket);
	bool DoRecv(RUDPSession& session);
	int MakeSendStream(OUT RUDPSession& session, OUT IOContext* context, ThreadIdType threadId);

private:
	RIO_EXTENSION_FUNCTION_TABLE rioFunctionTable{};
	RIO_CQ* rioCQList = nullptr;

private:
	CTLSMemoryPool<IOContext> contextPool;
#pragma endregion RIO

private:
	void EncodePacket(OUT NetBuffer& packet);
	WORD GetPayloadLength(OUT NetBuffer& buffer);
};

static CTLSMemoryPool<SendPacketInfo>* sendPacketInfoPool = new CTLSMemoryPool<SendPacketInfo>(2, false);
