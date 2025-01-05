#pragma once
#include "NetServerSerializeBuffer.h"
#include "NetClient.h"
#include "../MultiSocketRUDPServer/BuildConfig.h"
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
	explicit RecvPacketInfo(NetBuffer* inBuffer, PacketSequence inPacketSequence)
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
	bool Start(const std::wstring& optionFilePath);
	void Stop();

	bool IsStopped();
	bool IsConnected();

private:
	bool ConnectToServer();

private:
	bool isStopped{};
	bool isConnected{};

#pragma region SessionBroker
#if USE_IOCP_SESSION_BROKER
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
	bool ReadOptionFile(const std::wstring& optionFilePath);
	bool GetSessionFromServer();

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

#pragma endregion SessionBroker

#pragma region RUDP
private:
	void RunThreads();
	void RunRecvThread();
	void RunSendThread();

	void ProcessRecvPacket(OUT NetBuffer& receivedBuffer);
	void DoSend();

private:
	SOCKET rudpSocket{};
	sockaddr_in serverAddr{};

	std::thread recvThread{};
	std::thread sendThread{};
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

	PacketSequence recvPacketSequence{};
#pragma endregion RUDP

public:
	unsigned int GetRemainPacketSize();
	NetBuffer* GetReceivedPacket();
	void SendPacket(OUT IPacket& packet);

private:
	void SendPacket(OUT NetBuffer& buffer, const PacketSequence inSendPacketSequence);
	WORD GetPayloadLength(OUT NetBuffer& buffer);
	void EncodePacket(OUT NetBuffer& packet);

private:
	CListBaseQueue<NetBuffer*> sendBufferQueue;
	std::mutex sendBufferQueueLock;
};

static CTLSMemoryPool<SendPacketInfo>* sendPacketInfoPool = new CTLSMemoryPool<SendPacketInfo>(2, false);
