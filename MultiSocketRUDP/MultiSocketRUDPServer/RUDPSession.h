#pragma once
#include <functional>
#include <map>
#include "CoreType.h"
#include <MSWSock.h>
#include "NetServerSerializeBuffer.h"
#include "Queue.h"
#include <shared_mutex>
#include <unordered_set>
#include <queue>
#include "PacketManager.h"
#include "RUDPFlowController.h"

class MultiSocketRUDPCore;
class IPacket;

struct SendPacketInfo;
struct IOContext;

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
	std::mutex sendPacketInfoQueueLock;
	std::queue<SendPacketInfo*> sendPacketInfoQueue;
	SendPacketInfo* reservedSendPacketInfo;
	char rioSendBuffer[MAX_SEND_BUFFER_SIZE];
	RIO_BUFFERID sendBufferId;
	IO_MODE ioMode = IO_MODE::IO_NONE_SENDING;
};

class RUDPSession
{
	friend MultiSocketRUDPCore;

protected:
	RUDPSession() = delete;
	explicit RUDPSession(MultiSocketRUDPCore& inCore);

private:
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
	void DoDisconnect();
	bool SendPacket(IPacket& packet);

private:
	void OnConnected(SessionIdType inSessionId);
	virtual void OnConnected() {}
	virtual void OnDisconnected() {}
	[[nodiscard]]
	inline bool SendPacket(NetBuffer& buffer, PacketSequence inSendPacketSequence, bool isReplyType, bool isCorePacket);

	void SendHeartbeatPacket();

	bool CheckReservedSessionTimeout(unsigned long long now) const;
	void AbortReservedSession();
	void CloseSocket();
	void UnregisterRIOBuffers();
	static void SetMaximumPacketHoldingQueueSize(BYTE size);

private:
	void TryConnect(NetBuffer& recvPacket, const sockaddr_in& inClientAddr);
	void Disconnect();
	// Call this function when the client sends a disconnect packet
	void Disconnect(NetBuffer& recvPacket);
	[[nodiscard]]
	bool OnRecvPacket(NetBuffer& recvPacket);
	[[nodiscard]]
	bool ProcessPacket(NetBuffer& recvPacket, PacketSequence recvPacketSequence);
	[[nodiscard]]
	bool ProcessHoldingPacket();
	void SendReplyToClient(PacketSequence recvPacketSequence);
	void OnSendReply(NetBuffer& recvPacket);

private:
	[[nodiscard]]
	bool CanProcessPacket(const sockaddr_in& targetClientAddr) const;
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
	bool IsConnected() const { return sessionState == SESSION_STATE::CONNECTED; }
	[[nodiscard]]
	bool IsReserved() const { return sessionState == SESSION_STATE::RESERVED; }
	[[nodiscard]]
	bool IsUsingSession() const { return sessionState == SESSION_STATE::RESERVED || sessionState == SESSION_STATE::CONNECTED; }

protected:
	using PacketFactory = std::function<std::function<bool()>(RUDPSession*, NetBuffer*)>;

	static std::shared_ptr<IPacket> BufferToPacket(NetBuffer& buffer, const PacketId packetId)
	{
		std::shared_ptr<IPacket> packet = PacketManager::GetInst().MakePacket(packetId);
		if (packet != nullptr)
		{
			packet->BufferToPacket(buffer);
		}

		return packet;
	}

	template <typename DerivedType, typename PacketType>
	void RegisterPacketHandler(const PacketId packetId, void (DerivedType::* func)(const PacketType&))
	{
		static_assert(std::is_base_of_v<IPacket, PacketType>, "PacketType must be derived from IPacket");
		packetFactoryMap[packetId] = [func, packetId](RUDPSession* session, NetBuffer* buffer)
			-> std::function<bool()>
			{
				DerivedType* derived = static_cast<DerivedType*>(session);
				if (auto packet = BufferToPacket(*buffer, packetId); packet != nullptr)
				{
					return [derived, func, packet]()
						{
							(derived->*func)(static_cast<PacketType&>(*packet));
							return true;
						};
				}

				return []() { return false; };
			};
	}

private:
	std::unordered_map<PacketId, PacketFactory> packetFactoryMap;

private:
	std::atomic<SESSION_STATE> sessionState{ SESSION_STATE::DISCONNECTED };
	SessionIdType sessionId = INVALID_SESSION_ID;
	// a connectKey seems to be necessary
	// generate and store a key on the TCP connection side,
	// then insert the generated key into the packet and send it
	// if the connectKey matches, verifying it as a valid key,
	// insert the client information into clientAddr below
	unsigned char sessionKey[SESSION_KEY_SIZE];
	unsigned char sessionSalt[SESSION_SALT_SIZE];
	unsigned char* keyObjectBuffer{};
	BCRYPT_KEY_HANDLE sessionKeyHandle{};
	sockaddr_in clientAddr{};
	SOCKADDR_INET clientSockAddrInet{};
	PortType serverPort{ INVALID_PORT_NUMBER };
	SOCKET sock{};
	mutable std::shared_mutex socketLock;
	bool nowInReleaseThread{};
	std::atomic_bool nowInProcessingRecvPacket{};
	ThreadIdType threadId{};
	RUDPFlowController flowController;

	std::atomic<PacketSequence> lastSendPacketSequence{};
	std::map<PacketSequence, SendPacketInfo*> sendPacketInfoMap;
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
	static BYTE maximumHoldingPacketQueueSize;
	std::atomic_uchar sequenceViolationCounter{};

	unsigned long long sessionReservedTime{};
	static unsigned long long constexpr RESERVED_SESSION_TIMEOUT_MS = 30000;

private:
	RecvBuffer recvBuffer;
	SendBuffer sendBuffer;

	RIO_RQ rioRQ = RIO_INVALID_RQ;

private:
	MultiSocketRUDPCore& core;
};

using SessionFactoryFunc = std::function<RUDPSession*(MultiSocketRUDPCore&)>;
