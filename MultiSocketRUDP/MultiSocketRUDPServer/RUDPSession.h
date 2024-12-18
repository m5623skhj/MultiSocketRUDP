#pragma once
#include "CoreType.h"
#include <MSWSock.h>
#include "LockFreeQueue.h"
#include "NetServerSerializeBuffer.h"
#include "Queue.h"
#include <shared_mutex>
#include <unordered_map>
#include <queue>

class MultiSocketRUDPCore;
class IPacket;

struct SendPacketInfo;

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

enum class IO_MODE : LONG
{
	IO_NONE_SENDING = 0
	, IO_SENDING
};

struct RecvBuffer
{
	char buffer[recvBufferSize];
	RIO_BUFFERID recvBufferId{};
	CListBaseQueue<NetBuffer*> recvBufferList;
};

struct SendBuffer
{
	WORD bufferCount = 0;
	CLockFreeQueue<SendPacketInfo*> sendPacketInfoQueue;
	SendPacketInfo* reservedSendPacketInfo;
	char rioSendBuffer[maxSendBufferSize];
	RIO_BUFFERID sendBufferId;
	IO_MODE ioMode = IO_MODE::IO_NONE_SENDING;
};

class RUDPSession
{
	friend MultiSocketRUDPCore;

private:
	RUDPSession() = delete;
	explicit RUDPSession(SOCKET inSock, PortType inServerPort, MultiSocketRUDPCore& inCore);

	static std::shared_ptr<RUDPSession> Create(SOCKET inSock, PortType inPort, MultiSocketRUDPCore& inCore);
	bool InitializeRIO(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable, RIO_CQ& rioRecvCQ, RIO_CQ& rioSendCQ);
	void InitializeSession();

public:
	virtual ~RUDPSession();

public:
	void Disconnect();
	inline bool SendPacket(IPacket& packet);

private:
	void OnConnected(SessionIdType inSessionId);
	void OnDisconnected();
	inline bool SendPacket(NetBuffer& buffer, const PacketSequence inSendPacketSequence);

private:
	void TryConnect(NetBuffer& recvPacket);
	void Disconnect(NetBuffer& recvPacket);
	bool OnRecvPacket(NetBuffer& recvPacket);
	bool ProcessPacket(NetBuffer& recvPacket);
	bool ProcessHoldingPacket();
	void OnSendReply(NetBuffer& recvPacket);

private:
	bool CheckMyClient(const sockaddr_in& targetClientAddr);

public:

	sockaddr_in GetSocketAddress();

private:
	std::atomic_bool isConnected{};
	SessionIdType sessionId = invalidSessionId;
	// a connectKey seems to be necessary
	// generate and store a key on the TCP connection side,
	// then insert the generated key into the packet and send it
	// if the connectKey matches, verifying it as a valid key,
	// insert the client information into clientAddr below
	std::string sessionKey{};
	sockaddr_in clientAddr{};
	PortType serverPort{ invalidPortNumber };
	SOCKET sock{};
	bool isUsingSession{};
	bool ioCancle{};
	ThreadIdType threadId{};

	std::atomic<PacketSequence> lastSendPacketSequence{};
	std::unordered_map<PacketSequence, SendPacketInfo*> sendPacketInfoMap;
	std::shared_mutex sendPacketInfoMapLock;

	std::atomic<PacketSequence> lastReceivedPacketSequence{};
	struct RecvPacketInfoPriority
	{
		bool operator()(const RecvPacketInfo& lfh, const RecvPacketInfo& rfh)
		{
			return lfh.packetSequence > rfh.packetSequence;
		}
	};
	std::priority_queue<RecvPacketInfo, std::vector<RecvPacketInfo>, RecvPacketInfoPriority> recvPacketHolderQueue;
	std::recursive_mutex recvPacketHolderQueueLock;

private:
	RecvBuffer recvBuffer;
	SendBuffer sendBuffer;

	RIO_RQ rioRQ = RIO_INVALID_RQ;

private:
	MultiSocketRUDPCore& core;
};
