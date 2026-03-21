#pragma once
#include <functional>
#include "../Common/etc/CoreType.h"
#include <shared_mutex>
#include "PacketManager.h"
#include "../Common/FlowController/RUDPFlowManager.h"
#include "SessionCryptoContext.h"
#include "SessionPacketOrderer.h"
#include "SessionSocketContext.h"
#include "SessionRIOContext.h"
#include "SessionStateMachine.h"

namespace MultiSocketRUDP
{
	struct PacketSequenceSetKey;
}

class MultiSocketRUDPCore;
class RUDPSessionFunctionDelegate;
class IPacket;

struct SendPacketInfo;
struct RecvBuffer;
struct IOContext;

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
	void InitializeSession();

	void SetSessionId( const SessionIdType inSessionId);
	void SetThreadId(const ThreadIdType inThreadId);

public:
	virtual ~RUDPSession() = default;

public:
	// ----------------------------------------
	// @brief 현재 세션을 RELEASING 상태로 전이시키고 연결 해제 프로세스를 시작합니다.
	// @details 예약 상태 또는 연결 상태에서만 RELEASING 상태로 전이할 수 있습니다.
	// ----------------------------------------
	void DoDisconnect();
	bool SendPacket(IPacket& packet);

	ThreadIdType GetThreadId() const;

private:
	void OnConnected(SessionIdType inSessionId);
	virtual void OnConnected() {}
	virtual void OnDisconnected() {}
	virtual void OnReleased() {}
	[[nodiscard]]
	bool SendPacket(NetBuffer& buffer, PacketSequence inSendPacketSequence, bool isReplyType, bool isCorePacket);
	// ----------------------------------------
	// @brief 보류 큐를 거치지 않고 패킷을 즉시 전송합니다.직접 RIO Send 작업을 예약합니다.
	// @param buffer 전송할 NetBuffer.
	// @param inSendPacketSequence 전송할 패킷의 시퀀스 번호.
	// @param isReplyType 응답 패킷인지 여부.
	// @param isCorePacket 코어 기능 관련 패킷인지 여부.
	// @return RIO Send 작업이 성공적으로 예약되면 true, 아니면 false.
	// ----------------------------------------
	[[nodiscard]]
	bool SendPacketImmediate(NetBuffer& buffer, PacketSequence inSendPacketSequence, bool isReplyType, bool isCorePacket);
	// ----------------------------------------
	// @brief 플로우 제어에 의해 보류된 패킷들을 전송 가능한지 확인하고 전송을 시도합니다.
	// ----------------------------------------
	void TryFlushPendingQueue();

	void SendHeartbeatPacket();

	// ----------------------------------------
	// @brief 예약된 세션이 타임아웃되었는지 확인합니다.
	// @param now 현재 시간 (밀리초)
	// @return 타임아웃되었으면 true, 아니면 false
	// ----------------------------------------
	[[nodiscard]]
	bool CheckReservedSessionTimeout(unsigned long long now) const;
	// ----------------------------------------
	// @brief 예약 상태의 세션을 강제로 중단시키고 RELEASING 상태로 전이합니다.
	// @details 주로 예약 세션 타임아웃 시 호출됩니다.
	// ----------------------------------------
	void AbortReservedSession();
	void CloseSocket();
	static void SetMaximumPacketHoldingQueueSize(BYTE size);
	void EnqueueToRecvBufferList(NetBuffer* buffer);
	RecvBuffer& GetRecvBuffer();

	void RecvContextReset();
	std::shared_ptr<IOContext> GetRecvBufferContext() const;

	RIO_RQ GetRecvRIORQ() const;
	RIO_RQ GetSendRIORQ() const;

private:
	bool TryConnect(NetBuffer& recvPacket, const sockaddr_in& inClientAddr);
	// ----------------------------------------
	// @brief RELEASING 상태의 세션을 최종적으로 해제하고 DISCONNECTED 상태로 전환합니다.
	// @details 세션의 소켓을 닫고 리소스 풀로 반환합니다.
	// ----------------------------------------
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
	// ----------------------------------------
	// @brief 세션이 현재 연결 상태인지 확인합니다.
	// @return 연결 상태이면 true, 아니면 false
	// ----------------------------------------
	[[nodiscard]]
	bool IsConnected() const;
	// ----------------------------------------
	// @brief 세션이 현재 예약 상태인지 확인합니다.
	// @return 예약 상태이면 true, 아니면 false
	// ----------------------------------------
	[[nodiscard]]
	bool IsReserved() const;
	// ----------------------------------------
	// @brief 세션이 현재 사용 중 (예약 또는 연결) 상태인지 확인합니다.
	// @return 사용 중이면 true, 아니면 false
	// ----------------------------------------
	[[nodiscard]]
	bool IsUsingSession() const;
	// ----------------------------------------
	// @brief 현재 세션의 상태를 반환합니다.
	// @return 현재 세션의 SESSION_STATE 값
	// ----------------------------------------
	[[nodiscard]]
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
	sockaddr_in clientAddr{};
	SOCKADDR_INET clientSockAddrInet{};
	std::atomic_bool nowInReleaseThread{};
	std::atomic_bool nowInProcessingRecvPacket{};
	ThreadIdType threadId{};

	static BYTE maximumHoldingPacketQueueSize;

	unsigned long long sessionReservedTime{};
	static unsigned long long constexpr RESERVED_SESSION_TIMEOUT_MS = 30000;

private:
	SessionCryptoContext& GetCryptoContext();
	const SessionCryptoContext& GetCryptoContext() const;

	SessionSendContext& GetSendContext();
	const SessionSendContext& GetSendContext() const;

private:
	RUDPFlowManager flowManager;
	SessionCryptoContext cryptoContext;
	SessionPacketOrderer sessionPacketOrderer;
	SessionSocketContext socketContext;
	SessionRIOContext rioContext;
	SessionStateMachine stateMachine;

private:
	MultiSocketRUDPCore& core;
};
