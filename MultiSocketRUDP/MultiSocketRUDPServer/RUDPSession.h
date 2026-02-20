#pragma once
#include <functional>
#include <set>
#include "../Common/etc/CoreType.h"
#include "Queue.h"
#include <shared_mutex>
#include "PacketManager.h"
#include "RIOManager.h"
#include "../Common/FlowController/RUDPFlowManager.h"
#include "SessionCryptoContext.h"
#include "SessionSendContext.h"
#include "SessionPacketOrderer.h"

namespace MultiSocketRUDP
{
	struct PacketSequenceSetKey;
}

class MultiSocketRUDPCore;
class RUDPSessionFunctionDelegate;
class IPacket;

struct SendPacketInfo;
struct IOContext;

struct RecvBuffer
{
	std::shared_ptr<IOContext> recvContext{};
	char buffer[RECV_BUFFER_SIZE];
	CListBaseQueue<NetBuffer*> recvBufferList;
};

class RUDPSession
{
	friend MultiSocketRUDPCore;
	friend RUDPSessionFunctionDelegate;

protected:
	explicit RUDPSession(MultiSocketRUDPCore& inCore);

private:
	[[nodiscard]]
	bool InitializeRIO(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable, const RIO_CQ& rioRecvCQ, const RIO_CQ& rioSendCQ);
	[[nodiscard]]
	bool InitRIOSendBuffer(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable);
	[[nodiscard]]
	bool InitRIORecvBuffer(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable);
	void InitializeSession();

	void SetSessionId( const SessionIdType inSessionId);
	void SetThreadId(const ThreadIdType inThreadId);

public:
	virtual ~RUDPSession();

public:
	void DoDisconnect();
	bool SendPacket(IPacket& packet);

	ThreadIdType GetThreadId() const;

private:
	void OnConnected(SessionIdType inSessionId);
	virtual void OnConnected() {}
	virtual void OnDisconnected() {}
	virtual void OnReleased() {}
	[[nodiscard]]
	inline bool SendPacket(NetBuffer& buffer, PacketSequence inSendPacketSequence, bool isReplyType, bool isCorePacket);

	void SendHeartbeatPacket();

	[[nodiscard]]
	bool CheckReservedSessionTimeout(unsigned long long now) const;
	void AbortReservedSession();
	void CloseSocket();
	static void UnregisterRIOBuffer(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable, OUT RIO_BUFFERID& bufferId);
	void UnregisterRIOBuffers() const;
	static void SetMaximumPacketHoldingQueueSize(BYTE size);
	void EnqueueToRecvBufferList(NetBuffer* buffer);
	RecvBuffer& GetRecvBuffer();

	void RecvContextReset();
	std::shared_ptr<IOContext> GetRecvBufferContext() const;

	RIO_RQ GetRecvRIORQ() const;
	RIO_RQ GetSendRIORQ() const;

private:
	bool TryConnect(NetBuffer& recvPacket, const sockaddr_in& inClientAddr);
	void Disconnect();
	// Call this function when the client sends a disconnect packet
	void Disconnect(NetBuffer& recvPacket);
	[[nodiscard]]
	bool OnRecvPacket(NetBuffer& recvPacket);
	[[nodiscard]]
	bool ProcessPacket(NetBuffer& recvPacket, PacketSequence recvPacketSequence);
	void SendReplyToClient(PacketSequence recvPacketSequence);
	void OnSendReply(NetBuffer& recvPacket);

private:
	[[nodiscard]]
	bool CanProcessPacket(const sockaddr_in& targetClientAddr) const;
	[[nodiscard]]
	bool CheckMyClient(const sockaddr_in& targetClientAddr) const;

private:
	std::shared_mutex& GetSocketMutex() const;

public:
	[[nodiscard]]
	SessionIdType GetSessionId() const;
	[[nodiscard]]
	SOCKET GetSocket() const;
	[[nodiscard]]
	sockaddr_in GetSocketAddress() const;
	[[nodiscard]]
	SOCKADDR_INET GetSocketAddressInet() const;
	[[nodiscard]]
	SOCKADDR_INET& GetSocketAddressInetRef();
	[[nodiscard]]
	bool IsConnected() const;
	[[nodiscard]]
	bool IsReserved() const;
	[[nodiscard]]
	bool IsUsingSession() const;
	SESSION_STATE GetSessionState() const;
	[[nodiscard]]
	bool IsReleasing() const;

protected:
	using PacketFactory = std::function<std::function<bool()>(RUDPSession*, NetBuffer*)>;

	[[nodiscard]]
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
	SessionIdType sessionId = INVALID_SESSION_ID;
	std::atomic<SESSION_STATE> sessionState{ SESSION_STATE::DISCONNECTED };
	sockaddr_in clientAddr{};
	SOCKADDR_INET clientSockAddrInet{};
	PortType serverPort{ INVALID_PORT_NUMBER };
	SOCKET sock{};
	mutable std::shared_mutex socketLock;
	bool nowInReleaseThread{};
	std::atomic_bool nowInProcessingRecvPacket{};
	ThreadIdType threadId{};

	static BYTE maximumHoldingPacketQueueSize;

	std::set<MultiSocketRUDP::PacketSequenceSetKey> cachedSequenceSet;
	std::mutex cachedSequenceSetLock;

	unsigned long long sessionReservedTime{};
	static unsigned long long constexpr RESERVED_SESSION_TIMEOUT_MS = 30000;

private:
	RecvBuffer recvBuffer;

	RIO_RQ rioRQ = RIO_INVALID_RQ;

private:
	SessionCryptoContext& GetCryptoContext();
	const SessionCryptoContext& GetCryptoContext() const;

	SessionSendContext& GetSendContext();
	const SessionSendContext& GetSendContext() const;

private:
	RUDPFlowManager flowManager;
	SessionSendContext sendContext;
	SessionCryptoContext cryptoContext;
	SessionPacketOrderer sessionPacketOrderer;

private:
	MultiSocketRUDPCore& core;
};