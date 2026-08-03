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
#include "RetransmissionTimeoutEstimator.h"

namespace MultiSocketRUDP
{
	struct PacketSequenceSetKey;
}

class MultiSocketRUDPCore;
class RUDPSessionFunctionDelegate;
class RUDPIOHandler;
class IPacket;

struct SendPacketInfo;
struct RecvBuffer;
struct IOContext;

class RUDPSession
{
	friend MultiSocketRUDPCore;
	friend RUDPSessionFunctionDelegate;
	friend RUDPIOHandler;
	friend class RUDPSessionBehaviorAccess;

protected:
	explicit RUDPSession(MultiSocketRUDPCore& inCore);

private:
	[[nodiscard]]
	bool InitializeRIO(const RIO_EXTENSION_FUNCTION_TABLE& rioFunctionTable, const RIO_CQ& rioRecvCQ, const RIO_CQ& rioSendCQ);
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
	void DoDisconnect(const DISCONNECT_REASON disconnectSession);
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

	void SendHeartbeatPacket(const unsigned long long now);
	void RefreshLastReceivedPacketTime(unsigned long long now);
	[[nodiscard]]
	bool NeedToSendHeartbeat(unsigned long long now) const;

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
	// ----------------------------------------
	// @brief 해제 세션의 수신 로직 drain 후 OnDisconnected와 소켓 종료를 한 번만 수행합니다.
	// @details 수신 로직이 남아 있으면 상태를 변경하지 않고 다음 해제 반복에서 다시 시도합니다.
	// ----------------------------------------
	void BeginIOShutdown();
	// ----------------------------------------
	// @brief 완료 처리 진입을 기록하여 세션의 조기 풀 반환을 방지합니다.
	// ----------------------------------------
	void BeginIOCompletion();
	// ----------------------------------------
	// @brief 완료 처리 이탈을 기록합니다. BeginIOCompletion 호출과 정확히 한 번 대응해야 합니다.
	// ----------------------------------------
	void CompleteIOCompletion();
	// ----------------------------------------
	// @brief drain 완료 후 RIO 등록 자원과 컨텍스트를 정리합니다.
	// ----------------------------------------
	void FinalizeRIOCleanup();
	// ----------------------------------------
	// @brief 수신 I/O, 수신 로직, 송신, 완료 처리가 모두 끝났는지 확인합니다.
	// @return 세션을 안전하게 최종 해제할 수 있으면 true입니다.
	// ----------------------------------------
	[[nodiscard]]
	bool CanFinalizeIO();
	static void SetMaximumPacketHoldingQueueSize(BYTE size);
	RecvBuffer& GetRecvBuffer();

	void RecvContextReset();
	std::shared_ptr<IOContext> GetRecvBufferContext() const;

	RIO_RQ GetRecvRIORQ() const;
	RIO_RQ GetSendRIORQ() const;

private:
	bool TryConnect(NetBuffer& recvPacket, const sockaddr_in& inClientAddr);
	// ----------------------------------------
	// @brief RELEASING 상태의 세션을 최종적으로 해제하고 DISCONNECTED 상태로 전환합니다.
	// @details 소켓 종료가 시작됐고 모든 RIO/로직 작업이 drain된 경우에만 RIO 리소스를 정리하고 풀로 반환합니다.
	// ----------------------------------------
	void Disconnect();
	// Call this function when the client sends a disconnect packet
	void Disconnect(NetBuffer& recvPacket);
	[[nodiscard]]
	bool OnRecvPacket(NetBuffer& recvPacket);
	[[nodiscard]]
	bool ProcessPacket(NetBuffer& recvPacket, PacketSequence recvPacketSequence);
	[[nodiscard]]
	static bool IsOlderRecvSequence(PacketSequence sequence, PacketSequence expectedSequence) noexcept;
	void SendReplyToClient(PacketSequence recvPacketSequence);
	void OnSendReply(NetBuffer& recvPacket);
	void OnRetransmissionTimeout() noexcept;
	void OnRttSample(std::chrono::steady_clock::duration sample);

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
	// @brief Returns current retransmission timeout calculated for this session.
	// @return RTO in milliseconds.
	// ----------------------------------------
	[[nodiscard]]
	unsigned int GetRetransmissionTimeoutMs() const noexcept;
	// ----------------------------------------
	// @brief 현재 세션의 상태를 반환합니다.
	// @return 현재 세션의 SESSION_STATE 값
	// ----------------------------------------
	[[nodiscard]]
	SESSION_STATE GetSessionState() const;
	[[nodiscard]]
	bool IsReleasing() const;
	[[nodiscard]]
	uint32_t GetSessionGeneration() const;

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
	std::atomic_bool ioShutdownStarted{};
	std::atomic_uint32_t activeIOCompletions{};
	ThreadIdType threadId{};
	std::atomic_uint32_t sessionGeneration{};

	static BYTE maximumHoldingPacketQueueSize;
	static unsigned long long reservedSessionTimeoutMs;

	unsigned long long onSessionReleaseTime{};
	unsigned long long sessionReservedTime{};
	std::atomic_ullong lastReceivedPacketTime{};

public:
	void SetStateMachineToDisconnect();
	DISCONNECT_REASON GetDisconnectedReason() const;

#if _DEBUG
	static void SetReservedSessionTimeoutMsForTest(unsigned long long inTimeoutMs);
	static unsigned long long GetReservedSessionTimeoutMsForTest();
#endif

private:
	DISCONNECT_REASON disconnectedReason{ DISCONNECT_REASON::NOT_DISCONNECTED };

private:
	SessionCryptoContext& GetCryptoContext();
	const SessionCryptoContext& GetCryptoContext() const;

	SessionSendContext& GetSendContext();
	const SessionSendContext& GetSendContext() const;

private:
	RUDPFlowManager flowManager;
	RetransmissionTimeoutEstimator retransmissionTimeoutEstimator;
	SessionCryptoContext cryptoContext;
	SessionPacketOrderer sessionPacketOrderer;
	SessionSocketContext socketContext;
	SessionRIOContext rioContext;
	SessionStateMachine stateMachine;

private:
	MultiSocketRUDPCore& core;
};
