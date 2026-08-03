#pragma once
#include "IIOHandler.h"
#include "RetransmissionScheduler.h"
#include <vector>
#include <mutex>
#include <memory>
#include <set>
#include <random>

namespace MultiSocketRUDP
{
	struct PacketSequenceSetKey;
}

class IRIOManager;
class ISessionDelegate;
class ICore;
class RUDPSession;

struct IOContext;
struct SendPacketInfo;

enum class SEND_PACKET_INFO_TO_STREAM_RETURN : char
{
	SUCCESS = 0,
	OCCURED_ERROR = -1,
	IS_ERASED_PACKET = -2,
	STREAM_IS_FULL = -3,
	IS_SENT = -4,
};

class DatagramLossSimulator
{
public:
	DatagramLossSimulator(const unsigned int inLossPercent, const int inSeed)
		: lossRate(inLossPercent / 100.0)
		, recvEngine(static_cast<unsigned int>(inSeed))
		, sendEngine(static_cast<unsigned int>(inSeed) ^ 0x9E3779B9u)
	{
	}

	[[nodiscard]]
	bool ShouldDropReceivedDatagram()
	{
		std::scoped_lock lock(recvLock);
		return std::bernoulli_distribution(lossRate)(recvEngine);
	}

	[[nodiscard]]
	bool ShouldDropSendingDatagram()
	{
		std::scoped_lock lock(sendLock);
		return std::bernoulli_distribution(lossRate)(sendEngine);
	}

private:
	double lossRate;
	std::mt19937 recvEngine;
	std::mt19937 sendEngine;
	std::mutex recvLock;
	std::mutex sendLock;
};

// RUDPIOHandler 클래스는 RIO(Registered I/O) 기반의 네트워크 통신을 처리하는 핸들러입니다.
// 세션 관리, 패킷 송수신, 재전송 처리 등 RUDP 프로토콜의 핵심 I/O 로직을 담당합니다.
class RUDPIOHandler : public IIOHandler
{
public:
	RUDPIOHandler(IRIOManager& inRioManager
		, ISessionDelegate& inSessionDelegate
		, CTLSMemoryPool<IOContext>& contextPool
		, std::vector<std::unique_ptr<RetransmissionScheduler>>& retransmissionSchedulers
		, BYTE inMaxHoldingPacketQueueSize
		, unsigned int inRetransmissionMs
		, unsigned int inSimulatedPacketLossPercent = 0
		, int inSimulatedPacketLossSeed = 0
	);
	~RUDPIOHandler() override = default;

	RUDPIOHandler(const RUDPIOHandler&) = delete;
	RUDPIOHandler& operator=(const RUDPIOHandler&) = delete;
	RUDPIOHandler(RUDPIOHandler&&) = delete;
	RUDPIOHandler& operator=(RUDPIOHandler&&) = delete;

public:
	// ----------------------------------------
	// @brief 완료된 RIO 작업의 세션 소유권, generation, 상태를 검증하고 후속 처리를 분배합니다.
	// @details 유효한 세션의 completion은 drain barrier에 등록되며, 모든 반환 경로에서 등록이 해제됩니다.
	//          유효한 Recv 또는 Send의 stale·실패 completion도 해당 context를 정확히 한 번 반환합니다.
	// @param context 완료된 I/O의 소유권과 작업 종류가 기록된 컨텍스트입니다.
	// @param transferred 완료된 작업에서 송수신된 바이트 수입니다.
	// @param threadId 완료를 가져온 I/O worker ID입니다.
	// @param status RIORESULT 상태 코드입니다. 0이면 성공입니다.
	// @return completion을 처리하거나 안전하게 폐기했으면 true, 입력·작업 종류가 유효하지 않거나 후속 처리가 실패하면 false입니다.
	// ----------------------------------------
	[[nodiscard]]
	bool IOCompleted(IOContext* context, ULONG transferred, BYTE threadId, LONG status = 0) const override;
	// DoRecv 함수는 주어진 세션으로부터 데이터를 수신하는 작업을 시작합니다.
	// RIO 수신 요청을 트리거하고 필요한 경우 버퍼를 관리합니다.
	[[nodiscard]]
	bool DoRecv(RUDPSession& session) const override;
	// DoSend 함수는 주어진 세션에 데이터를 전송하는 작업을 시작합니다.
	// RIO 송신 요청을 트리거하고 전송 대기열을 관리합니다.
	[[nodiscard]]
	bool DoSend(OUT RUDPSession& session, ThreadIdType threadId) const override;

private:
	// ----------------------------------------
	// @brief 재사용된 세션에서 도착한 이전 generation 완료를 현재 세션 상태에 영향 없이 정리합니다.
	// @return 알려진 Recv 또는 Send context를 반환했으면 true, 작업 종류가 유효하지 않으면 false입니다.
	// ----------------------------------------
	[[nodiscard]]
	bool HandleStaleCompletion(IOContext* context) const;
	// ----------------------------------------
	// @brief 실패한 RIO 완료의 자원을 정리하고 필요하면 상태 코드에 맞는 세션 해제를 요청합니다.
	// @details 해제 중 발생한 WSA_OPERATION_ABORTED는 정상적인 취소로 처리하며 추가 종료를 요청하지 않습니다.
	// @return 알려진 Recv 또는 Send 오류 completion을 처리했으면 true, 작업 종류가 유효하지 않으면 false입니다.
	// ----------------------------------------
	[[nodiscard]]
	bool HandleFailedCompletion(IOContext* context, RUDPSession& session, LONG status) const;
	// ----------------------------------------
	// @brief 유효한 현재 generation 완료를 송신 또는 수신 완료 경로로 전달합니다.
	// @details 전용 완료 처리가 실패하면 해당 세션을 오류 해제 대상으로 전환합니다.
	// @return 전용 완료 처리가 성공하면 true, 처리 실패 또는 유효하지 않은 작업 종류이면 false입니다.
	// ----------------------------------------
	[[nodiscard]]
	bool DispatchSuccessfulCompletion(IOContext* context, RUDPSession& session, ULONG transferred, BYTE threadId) const;

	[[nodiscard]]
	bool RecvIOCompleted(OUT IOContext* contextResult, ULONG transferred, BYTE threadId) const;
	[[nodiscard]]
	bool SendIOCompleted(IOContext* context, BYTE threadId) const;
	// ----------------------------------------
	// @brief 컨텍스트를 원래 RecvBuffer의 자유 큐에 반환하고 수신 I/O 추적 수를 감소시킵니다.
	// ----------------------------------------
	void ReleaseRecvContext(IOContext* context) const;

	[[nodiscard]]
	bool TryRIOSend(OUT RUDPSession& session, IOContext* context) const;
	[[nodiscard]]
	std::pair<bool, IOContext*> MakeSendContext(OUT RUDPSession& session, ThreadIdType threadId) const;
	[[nodiscard]]
	std::pair<bool, unsigned int> MakeSendStream(OUT RUDPSession& session, ThreadIdType threadId) const;

	[[nodiscard]]
	SEND_PACKET_INFO_TO_STREAM_RETURN ReservedSendPacketInfoToStream(OUT RUDPSession& session, OUT std::set<MultiSocketRUDP::PacketSequenceSetKey>& packetSequenceSet, OUT unsigned int& totalSendSize, ThreadIdType threadId) const;
	[[nodiscard]]
	SEND_PACKET_INFO_TO_STREAM_RETURN StoredSendPacketInfoToStream(OUT RUDPSession& session, OUT std::set<MultiSocketRUDP::PacketSequenceSetKey>& packetSequenceSet, OUT unsigned int& totalSendSize, ThreadIdType threadId) const;

	[[nodiscard]]
	bool RefreshRetransmissionSendPacketInfo(OUT SendPacketInfo* sendPacketInfo, ThreadIdType threadId) const;

private:
	IRIOManager& rioManager;
	ISessionDelegate& sessionDelegate;
	CTLSMemoryPool<IOContext>& contextPool;
	std::vector<std::unique_ptr<RetransmissionScheduler>>& retransmissionSchedulers;

	unsigned int retransmissionMs {};

	std::unique_ptr<DatagramLossSimulator> lossSimulator;
};
