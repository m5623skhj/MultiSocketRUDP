#pragma once
#include "NetServerSerializeBuffer.h"
#include "BuildConfig.h"
#include "../Common/etc/CoreType.h"
#include <thread>
#include <mutex>
#include <array>
#include <map>
#include "ServerAliveChecker.h"

#include "Queue.h"
#include <queue>
#include "../Common/TLS/TLSHelper.h"

#pragma comment(lib, "ws2_32.lib")

class IPacket;

struct SendPacketInfo
{
	NetBuffer* buffer{};
	PacketRetransmissionCount retransmissionCount{};
	PacketSequence sendPacketSequence{};
	unsigned long long retransmissionTimeStamp{};
	std::list<SendPacketInfo*>::iterator listItor;
	std::atomic<int8_t> refCount{ 0 };

	void Initialize(NetBuffer* inBuffer, const PacketSequence inSendPacketSequence)
	{
		buffer = inBuffer;
		sendPacketSequence = inSendPacketSequence;
		retransmissionCount = {};
		retransmissionTimeStamp = {};
		NetBuffer::AddRefCount(inBuffer);
		refCount = 1;
	}

	void AddRefCount()
	{
		refCount.fetch_add(1, std::memory_order_relaxed);
	}

	static void Free(SendPacketInfo* target);

	[[nodiscard]]
	NetBuffer* GetBuffer() const { return buffer; }
};

struct RecvPacketInfo
{
	explicit RecvPacketInfo(NetBuffer* inBuffer, const PacketSequence inPacketSequence, const PACKET_TYPE inPacketType)
		: buffer(inBuffer)
		, packetSequence(inPacketSequence)
		, packetType(inPacketType)
	{
	}

	NetBuffer* buffer{};
	PacketSequence packetSequence{};
	PACKET_TYPE packetType{};
};

class RUDPClientCore
{
public:
	RUDPClientCore();
	virtual ~RUDPClientCore() = default;
	RUDPClientCore& operator=(const RUDPClientCore&) = delete;
	RUDPClientCore(RUDPClientCore&&) = delete;

protected:
	virtual bool Start(const std::wstring& clientCoreOptionFile, const std::wstring& sessionGetterOptionFilePath, bool printLogToConsole);
	virtual void Stop();
	void JoinThreads();

public:
	bool IsStopped() const { return isStopped; }
	bool IsConnected() const { return isConnected; }

private:
	bool CreateRUDPSocket();
	void SendConnectPacket();

private:
	bool isStopped{};
	std::atomic_bool threadStopFlag{};
	bool isConnected{};

#pragma region SessionGetter
#if USE_IOCP_SESSION_GETTER
private:
	class SessionGetter : public CNetClient
	{
	public:
		bool Start(const std::wstring& optionFilePath);

	private:
		virtual void OnConnectionComplete();
		virtual void OnRecv(CNetServerSerializationBuf* recvBuffer);
		virtual void OnSend(int sendsize);

		virtual void OnWorkerThreadBegin();
		virtual void OnWorkerThreadEnd();
		virtual void OnError(st_Error* error);
	};

	SessionGetter sessionGetter;
#else
private:
	bool RunGetSessionFromServer(const std::wstring& optionFilePath);
	bool GetSessionFromServer();
	bool TryConnectToSessionBroker() const;
	bool TrySetTargetSessionInfo();

private:
	WCHAR sessionBrokerIP[16]{};
	PortType sessionBrokerPort{};

	SOCKET sessionBrokerSocket{};

#endif

	TLSHelper::TLSHelperClient tlsHelper;

private:
	bool SetTargetSessionInfo(OUT NetBuffer& receivedBuffer);

private:
	std::string serverIp{};
	PortType port{};
	SessionIdType sessionId{};
	unsigned char sessionKey[SESSION_KEY_SIZE];
	unsigned char sessionSalt[SESSION_SALT_SIZE];
	unsigned char* keyObjectBuffer{};
	BCRYPT_KEY_HANDLE sessionKeyHandle{};

#pragma endregion SessionGetter

#pragma region RUDP
private:
	void RunThreads();
	void RunRecvThread();
	void RunSendThread();
	void RunRetransmissionThread();

	void OnRecvStream(NetBuffer& recvBuffer, int recvSize);
	void ProcessRecvPacket(OUT NetBuffer& receivedBuffer);
	void OnSendReply(NetBuffer& recvPacket, PacketSequence packetSequence);
	void SendReplyToServer(PacketSequence inRecvPacketSequence, PACKET_TYPE packetType = PACKET_TYPE::SEND_REPLY_TYPE);
	void DoSend();
	static void SleepRemainingFrameTime(OUT TickSet& tickSet, unsigned int intervalMs);

	PacketSequence GetNextRecvPacketSequence() const { return nextRecvPacketSequence; }

private:
	SOCKET rudpSocket{};
	sockaddr_in serverAddr{};

	std::jthread recvThread{};
	std::jthread sendThread{};
	std::jthread retransmissionThread{};
	std::array<HANDLE, 2> sendEventHandles{};

private:
	std::atomic<PacketSequence> lastSendPacketSequence{};
	std::map<PacketSequence, SendPacketInfo*> sendPacketInfoMap;
	std::mutex sendPacketInfoMapLock;

	struct RecvPacketInfoPriority
	{
		bool operator()(const RecvPacketInfo& lfh, const RecvPacketInfo& rfh) const
		{
			return lfh.packetSequence > rfh.packetSequence;
		}
	};
	std::priority_queue<RecvPacketInfo, std::vector<RecvPacketInfo>, RecvPacketInfoPriority> recvPacketHoldingQueue;
	std::mutex recvPacketHoldingQueueLock;

	std::atomic<BYTE> remoteAdvertisedWindow{ 1 };
	std::atomic<PacketSequence> lastAckedSequence{ 0 };
	PacketSequence nextRecvPacketSequence{ 1 };
#pragma endregion RUDP

public:
	unsigned int GetRemainPacketSize();
	NetBuffer* GetReceivedPacket();
	void SendPacket(OUT IPacket& packet);
	void Disconnect();

#if _DEBUG
	void SendPacketForTest(char* streamData, int streamSize);
#endif

private:
	// ----------------------------------------
	// @brief NetBuffer와 PacketSequence를 사용하여 SendPacketInfo를 할당, 초기화하고 전송 맵에 등록 후, 실제 전송 큐에 추가합니다.
	// @param buffer NetBuffer
	// @param inSendPacketSequence 패킷 순서 번호
	// ----------------------------------------
	void SendPacket(OUT NetBuffer& buffer, PacketSequence inSendPacketSequence, bool isCorePacket);
	// ----------------------------------------
	// @brief 흐름 제어 윈도우에 여유가 생겼을 때, 대기 중인 패킷 큐(pendingPacketQueue)에서 패킷을 가져와 전송을 시도합니다.
	// ----------------------------------------
	void SendPacket(const SendPacketInfo& sendPacketInfo);
	// ----------------------------------------
	// @brief NetBuffer를 SendPacketInfo에 등록하고, 재전송 큐에 추가하며 실제 전송을 위해 sendBufferQueue에 Enqueue합니다.
	// @param buffer 전송할 NetBuffer(이미 인코딩되어 있어야 함)
	// @param inSendPacketSequence 패킷 시퀀스 번호
	// ----------------------------------------
	void RegisterSendPacketInfo(NetBuffer& buffer, PacketSequence inSendPacketSequence);
	void TryFlushPendingQueue();
	static inline WORD GetPayloadLength(const NetBuffer& buffer);
	bool ReadOptionFile(const std::wstring& clientCoreOptionFile, const std::wstring& sessionGetterOptionFilePath);
	bool ReadClientCoreOptionFile(const std::wstring& optionFilePath);
	bool ReadSessionGetterOptionFile(const std::wstring& optionFilePath);

private:
	CListBaseQueue<NetBuffer*> sendBufferQueue;
	std::mutex sendBufferQueueLock;

	struct PendingPacketInfo
	{
		PacketSequence sequence;
		NetBuffer* buffer;

		bool operator>(const PendingPacketInfo& other) const
		{
			return sequence > other.sequence;
		}
	};

	std::priority_queue<PendingPacketInfo, std::vector<PendingPacketInfo>, std::greater<PendingPacketInfo>> pendingPacketQueue;
	std::mutex pendingPacketQueueLock;

private:
	PacketRetransmissionCount maxPacketRetransmissionCount{};
	unsigned int retransmissionThreadSleepMs{};
	unsigned int serverAliveCheckMs{};

private:
	ServerAliveChecker serverAliveChecker;
};

static auto sendPacketInfoPool = new CTLSMemoryPool<SendPacketInfo>(2, true);
