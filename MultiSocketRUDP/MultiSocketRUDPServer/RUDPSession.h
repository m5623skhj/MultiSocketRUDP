#pragma once
#include "CoreType.h"
#include <MSWSock.h>
#include "LockFreeQueue.h"
#include "NetServerSerializeBuffer.h"
#include "Queue.h"
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <queue>

class MultiSocketRUDPCore;
class IPacket;

struct SendPacketInfo;
struct IOContext;

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
	std::shared_ptr<IOContext> recvContext{};
	char buffer[RECV_BUFFER_SIZE];
	CListBaseQueue<NetBuffer*> recvBufferList;
};

struct SendBuffer
{
	CLockFreeQueue<SendPacketInfo*> sendPacketInfoQueue;
	SendPacketInfo* reservedSendPacketInfo;
	char rioSendBuffer[MAX_SEND_BUFFER_SIZE];
	RIO_BUFFERID sendBufferId;
	IO_MODE ioMode = IO_MODE::IO_NONE_SENDING;
};

class RUDPSession
{
	friend MultiSocketRUDPCore;

private:
	RUDPSession() = delete;
	explicit RUDPSession(MultiSocketRUDPCore& inCore);

	[[nodiscard]]
	bool InitializeRIO(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable, const RIO_CQ& rioRecvCQ, const RIO_CQ& rioSendCQ);
	[[nodiscard]]
	bool InitRIOSendBuffer(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable);
	[[nodiscard]]
	bool InitRIORecvBuffer(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable);
	void InitializeSession();

public:
	virtual ~RUDPSession();

public:
	void Disconnect();
	bool SendPacket(IPacket& packet);

private:
	void OnConnected(SessionIdType inSessionId);
	void OnDisconnected();
	[[nodiscard]]
	inline bool SendPacket(NetBuffer& buffer, const PacketSequence inSendPacketSequence, const bool isReplyType);

	void SendHeartbeatPacket();

private:
	void TryConnect(NetBuffer& recvPacket, const sockaddr_in& inClientAddr);
	// Call this function when the client sends a disconnect packet
	void Disconnect(NetBuffer& recvPacket);
	[[nodiscard]]
	bool OnRecvPacket(NetBuffer& recvPacket);
	[[nodiscard]]
	bool ProcessPacket(NetBuffer& recvPacket, const PacketSequence recvPacketSequence, const bool needReplyToClient = true);
	[[nodiscard]]
	bool ProcessHoldingPacket();
	void SendReplyToClient(const PacketSequence recvPacketSequence);
	void OnSendReply(NetBuffer& recvPacket);

private:
	[[nodiscard]]
	bool CheckMyClient(const sockaddr_in& targetClientAddr) const;
	[[nodiscard]]
	bool IsReleasing() const;

public:
	[[nodiscard]]
	SessionIdType GetSessionId() const;
	[[nodiscard]]
	sockaddr_in GetSocketAddress() const;
	[[nodiscard]]
	SOCKADDR_INET GetSocketAddressInet() const;
	[[nodiscard]]
	bool IsConnected() const;

private:
	std::atomic_bool isConnected{};
	SessionIdType sessionId = INVALID_SESSION_ID;
	// a connectKey seems to be necessary
	// generate and store a key on the TCP connection side,
	// then insert the generated key into the packet and send it
	// if the connectKey matches, verifying it as a valid key,
	// insert the client information into clientAddr below
	std::string sessionKey{};
	sockaddr_in clientAddr{};
	SOCKADDR_INET clientSockaddrInet{};
	PortType serverPort{ INVALID_PORT_NUMBER };
	SOCKET sock{};
	bool nowInReleaseThread{};
	bool nowInProcessingRecvPacket{};
	bool isUsingSession{};
	ThreadIdType threadId{};

	std::atomic<PacketSequence> lastSendPacketSequence{};
	std::unordered_map<PacketSequence, SendPacketInfo*> sendPacketInfoMap;
	std::shared_mutex sendPacketInfoMapLock;

	std::atomic<PacketSequence> nextRecvPacketSequence{};
	struct RecvPacketInfoPriority
	{
		bool operator()(const RecvPacketInfo& lfh, const RecvPacketInfo& rfh) const
		{
			return lfh.packetSequence > rfh.packetSequence;
		}
	};
	std::priority_queue<RecvPacketInfo, std::vector<RecvPacketInfo>, RecvPacketInfoPriority> recvPacketHolderQueue;
	std::unordered_multiset<PacketSequence> recvHoldingPacketSequences;

private:
	RecvBuffer recvBuffer;
	SendBuffer sendBuffer;

	RIO_RQ rioRQ = RIO_INVALID_RQ;

private:
	MultiSocketRUDPCore& core;
};
