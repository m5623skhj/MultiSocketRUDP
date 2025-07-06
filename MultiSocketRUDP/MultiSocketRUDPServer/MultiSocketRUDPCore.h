#pragma once
#include <optional>
#include <list>
#include <thread>
#include <MSWSock.h>
#include "NetServer.h"
#include "NetServerSerializeBuffer.h"
#include <shared_mutex>
#include "RUDPSession.h"
#include "Queue.h"
#include <vector>
#include <set>

#pragma comment(lib, "ws2_32.lib")

enum class SEND_PACKET_INFO_TO_STREAM_RETURN : char
{
	SUCCESS = 0,
	OCCURED_ERROR = -1,
	IS_ERASED_PACKET = -2,
	STREAM_IS_FULL = -3,
	IS_SENT = -4,
};

struct IOContext : RIO_BUF
{
	IOContext() = default;
	~IOContext() = default;

	inline void InitContext(SessionIdType inOwnerSessionId, RIO_OPERATION_TYPE inIOType);

	SessionIdType ownerSessionId = INVALID_SESSION_ID;
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
	PacketSequence sendPacketSequence{};
	unsigned long long retransmissionTimeStamp{};
	bool isErasedPacketInfo{};
	bool isReplyType{};
	std::list<SendPacketInfo*>::iterator listItor;

	~SendPacketInfo()
	{
		NetBuffer::Free(buffer);
		owner = {};
		retransmissionCount = {};
		sendPacketSequence = {};
		retransmissionTimeStamp = {};
		listItor = {};
		isErasedPacketInfo = {};
		isReplyType = {};
	}

	inline void Initialize(RUDPSession* inOwner, NetBuffer* inBuffer, const PacketSequence inSendPacketSequence, const bool inIsReplyType)
	{
		owner = inOwner;
		buffer = inBuffer;
		sendPacketSequence = inSendPacketSequence;
		isReplyType = inIsReplyType;
		NetBuffer::AddRefCount(inBuffer);
	}

	[[nodiscard]]
	inline NetBuffer* GetBuffer() const { return buffer; }
};

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

public:
	bool SendPacket(SendPacketInfo* sendPacketInfo);
	[[nodiscard]]
	bool DoRecv(const RUDPSession& session) const;
	[[nodiscard]]
	bool DoSend(OUT RUDPSession& session, const ThreadIdType threadId);
	[[nodiscard]]
	IOContext* MakeSendContext(OUT RUDPSession& session, const ThreadIdType threadId);
	[[nodiscard]]
	bool TryRIOSend(OUT RUDPSession& session, IOContext* context);
	// Never call this function directly. It should only be called within RDPSession::Disconnect()
	void DisconnectSession(const SessionIdType disconnectTargetSessionId);
	void EraseSendPacketInfo(OUT SendPacketInfo* eraseTarget, const ThreadIdType threadId);
	inline void PushToDisconnectTargetSession(RUDPSession& session);

private:
	[[nodiscard]]
	bool ReadOptionFile(const std::wstring& coreOptionFilePath, const std::wstring& sessionBrokerOptionFilePath);
	[[nodiscard]]
	bool InitNetwork();
	[[nodiscard]]
	bool InitRIO();
	[[nodiscard]]
	inline RIO_BUFFERID RegisterRIOBuffer(char* targetBuffer, const unsigned int targetBufferSize) const;
	[[nodiscard]]
	bool RunAllThreads();
	[[nodiscard]]
	bool RunSessionBroker();
	[[nodiscard]]
	SOCKET CreateRUDPSocket() const;

private:
	void CloseAllSessions();
	void ClearAllSession();

private:
	bool threadStopFlag{};
	bool isServerStopped{};
	unsigned short numOfSockets{};
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
	void RunSessionBrokerThread(const PortType listenPort, const std::string& rudpSessionIP);
	[[nodiscard]]
	bool OpenSessionBrokerSocket(const PortType listenPort);
	static void SetSessionKey(OUT RUDPSession& session);
	static void SetSessionInfoToBuffer(const RUDPSession& session, const std::string& rudpSessionIP, OUT NetBuffer& buffer);
	void ReserveSession(OUT NetBuffer& sendBuffer, const std::string& rudpSessionIP);
	[[nodiscard]]
	char InitReserveSession(RUDPSession& session) const;
	static void SendSessionInfoToClient(const SOCKET& clientSocket, OUT NetBuffer& sendBuffer);

private:
	std::jthread sessionBrokerThread{};
	SOCKET sessionBrokerListenSocket{ INVALID_SOCKET };
#endif

private:
	void RunIOWorkerThread(const ThreadIdType threadId);
	void RunRecvLogicWorkerThread(const ThreadIdType threadId);
	void RunRetransmissionThread(const ThreadIdType threadId);
	void RunSessionReleaseThread();
	void RunHeartbeatThread() const;
	FORCEINLINE static void SleepRemainingFrameTime(OUT TickSet& tickSet, const unsigned int intervalMs);

private:
	unsigned char numOfWorkerThread{};
	PacketRetransmissionCount maxPacketRetransmissionCount{};
	unsigned int workerThreadOneFrameMs{};
	unsigned int retransmissionMs{};
	unsigned int retransmissionThreadSleepMs{};
	unsigned int heartbeatThreadSleepMs{};

	// threads
	std::vector<std::jthread> ioWorkerThreads;
	std::vector<std::jthread> recvLogicWorkerThreads;
	std::vector<std::jthread> retransmissionThreads;
	std::jthread heartbeatThread;
	std::jthread sessionReleaseThread{};

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
	static bool ProcessByPacketType(RUDPSession& session, const sockaddr_in& clientAddr, NetBuffer& recvPacket);
	[[nodiscard]]
	unsigned int MakeSendStream(OUT RUDPSession& session, OUT IOContext* context, const ThreadIdType threadId);
    [[nodiscard]]  
	SEND_PACKET_INFO_TO_STREAM_RETURN ReservedSendPacketInfoToStream(OUT RUDPSession& session, OUT std::set<MultiSocketRUDP::PacketSequenceSetKey>& packetSequenceSet, OUT unsigned int& totalSendSize, const ThreadIdType threadId);
	[[nodiscard]]
	SEND_PACKET_INFO_TO_STREAM_RETURN StoredSendPacketInfoToStream(OUT RUDPSession& session, OUT std::set<MultiSocketRUDP::PacketSequenceSetKey>& packetSequenceSet, OUT unsigned int& totalSendSize, const ThreadIdType threadId);
	[[nodiscard]]
	bool RefreshRetransmissionSendPacketInfo(OUT SendPacketInfo* sendPacketInfo, const ThreadIdType threadId);

private:
	RIO_EXTENSION_FUNCTION_TABLE rioFunctionTable{};
	RIO_CQ* rioCQList = nullptr;

private:
	CTLSMemoryPool<IOContext> contextPool;
#pragma endregion RIO

private:
	static void EncodePacket(OUT NetBuffer& packet);
	[[nodiscard]]
	static inline WORD GetPayloadLength(OUT const NetBuffer& buffer);
};

static auto sendPacketInfoPool = new CTLSMemoryPool<SendPacketInfo>(2, true);
