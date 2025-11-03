#pragma once
#include "NetServerSerializeBuffer.h"
#include "NetClient.h"
#include "BuildConfig.h"
#include "../MultiSocketRUDPServer/CoreType.h"
#include <thread>
#include <mutex>
#include <array>
#include <map>

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

	void Initialize(NetBuffer* inBuffer, const PacketSequence inSendPacketSequence)
	{
		buffer = inBuffer;
		sendPacketSequence = inSendPacketSequence;
		NetBuffer::AddRefCount(inBuffer);
	}

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
	RUDPClientCore() = default;
	virtual ~RUDPClientCore() = default;
	RUDPClientCore& operator=(const RUDPClientCore&) = delete;
	RUDPClientCore(RUDPClientCore&&) = delete;

protected:
	virtual bool Start(const std::wstring& clientCoreOptionFile, const std::wstring& sessionGetterOptionFilePath, bool printLogToConsole);
	virtual void Stop();

public:
	bool IsStopped() const { return isStopped; }
	bool IsConnected() const { return isConnected; }

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
	std::vector<unsigned char> sessionKey{};
	std::vector<unsigned char> sessionSalt{};
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
	void SendPacket(OUT NetBuffer& buffer, PacketSequence inSendPacketSequence);
	void SendPacket(const SendPacketInfo& sendPacketInfo);
	static inline WORD GetPayloadLength(const NetBuffer& buffer);
	inline void EncodePacket(OUT NetBuffer& packet, const PacketSequence packetSequence, const PACKET_DIRECTION direction);
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

static auto sendPacketInfoPool = new CTLSMemoryPool<SendPacketInfo>(2, true);
