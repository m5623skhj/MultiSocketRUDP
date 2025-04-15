#pragma once
#include "NetServerSerializeBuffer.h"
#include "NetClient.h"
#include "BuildConfig.h"
#include "../MultiSocketRUDPServer/CoreType.h"
#include <thread>
#include <mutex>
#include <array>
#include "Queue.h"
#include <queue>
#include "../MultiSocketRUDPServer/PacketManager.h"

#pragma comment(lib, "ws2_32.lib")

struct SendPacketInfo
{
	NetBuffer* buffer{};
	PacketRetransmissionCount retransmissionCount{};
	PacketSequence sendPacektSequence{};
	unsigned long long sendTimeStamp{};
	std::list<SendPacketInfo*>::iterator listItor;

	void Initialize(NetBuffer* inBuffer, const PacketSequence inSendPacketSequence)
	{
		buffer = inBuffer;
		sendPacektSequence = inSendPacketSequence;
		NetBuffer::AddRefCount(inBuffer);
	}

	[[nodiscard]]
	NetBuffer* GetBuffer() { return buffer; }
};

struct RecvPacketInfo
{
	explicit RecvPacketInfo(NetBuffer* inBuffer, const PacketSequence inPacketSequence)
		: buffer(inBuffer)
		, packetSequence(inPacketSequence)
	{
	}

	NetBuffer* buffer{};
	PacketSequence packetSequence{};
};

class RUDPClientCore
{
private:
	RUDPClientCore() = default;
	~RUDPClientCore() = default;
	RUDPClientCore& operator=(const RUDPClientCore&) = delete;
	RUDPClientCore(RUDPClientCore&&) = delete;

public:
	static RUDPClientCore& GetInst();

public:
	bool Start(const std::wstring& clientCoreOptionFile, const std::wstring& sessionGetterOptionFilePath, const bool printLogToConsole);
	void Stop();

	inline bool IsStopped() const { return isStopped; }
	inline bool IsConnected() const { return isConnected; }

private:
	void StopThread(std::thread& stopTarget, const std::thread::id& threadId);

private:
	bool CreateRUDPSocket();
	void SendConnectPacket();

private:
	bool isStopped{};
	bool threadStopFlag{};
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
	bool TryConnectToSessionBroker();
	bool TrySetTargetSessionInfo();

private:
	WCHAR sessionBrokerIP[16]{};
	PortType sessionBrokerPort{};

	SOCKET sessionBrokerSocket{};

#endif
private:
	bool SetTargetSessionInfo(OUT NetBuffer& receivedBuffer);

private:
	std::string serverIp{};
	PortType port{};
	SessionIdType sessionId{};
	std::string sessionKey{};

#pragma endregion SessionGetter

#pragma region RUDP
private:
	void RunThreads();
	void RunRecvThread();
	void RunSendThread();
	void RunRetransmissionThread();

	void OnRecvStream(NetBuffer& recvBuffer, int recvSize);
	void ProcessRecvPacket(OUT NetBuffer& receivedBuffer);
	void OnSendReply(NetBuffer& recvPacket, const PacketSequence packetSequence);
	void SendReplyToServer(const PacketSequence recvPacketSequence);
	void DoSend();
	void SleepRemainingFrameTime(OUT TickSet& tickSet, const unsigned int intervalMs);

private:
	SOCKET rudpSocket{};
	sockaddr_in serverAddr{};

	std::thread recvThread{};
	std::thread sendThread{};
	std::thread retransmissionThread{};
	std::array<HANDLE, 2> sendEventHandles{};

private:
	std::atomic<PacketSequence> lastSendPacketSequence{};
	std::unordered_map<PacketSequence, SendPacketInfo*> sendPacketInfoMap;
	std::mutex sendPacketInfoMapLock;

	std::atomic<PacketSequence> lastReceivedPacketSequence{};
	struct RecvPacketInfoPriority
	{
		bool operator()(const RecvPacketInfo& lfh, const RecvPacketInfo& rfh)
		{
			return lfh.packetSequence > rfh.packetSequence;
		}
	};
	std::priority_queue<RecvPacketInfo, std::vector<RecvPacketInfo>, RecvPacketInfoPriority> recvPacketHoldingQueue;
	std::mutex recvPacketHoldingQueueLock;

	PacketSequence recvPacketSequence{ 1 };
#pragma endregion RUDP

public:
	unsigned int GetRemainPacketSize();
	NetBuffer* GetReceivedPacket();
	void SendPacket(OUT IPacket& packet);

private:
	void SendPacket(OUT NetBuffer& buffer, const PacketSequence inSendPacketSequence);
	inline WORD GetPayloadLength(OUT NetBuffer& buffer) const;
	inline void EncodePacket(OUT NetBuffer& packet);
	bool ReadOptionFile(const std::wstring& clientCoreOptionFile, const std::wstring& sessionGetterOptionFilePath);
	bool ReadClientCoreOptionFile(const std::wstring& optionFilePath);
	bool ReadSessionGetterOptionFile(const std::wstring& optionFilePath);

private:
	CListBaseQueue<NetBuffer*> sendBufferQueue;
	std::mutex sendBufferQueueLock;

private:
	PacketRetransmissionCount maxPacketRetransmissionCount{};
	unsigned int retransmissionThreadSleepMs{};
};

static CTLSMemoryPool<SendPacketInfo>* sendPacketInfoPool = new CTLSMemoryPool<SendPacketInfo>(2, false);
