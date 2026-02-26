#pragma once
#include "IIOHandler.h"
#include <vector>
#include <list>
#include <mutex>
#include <memory>
#include <set>

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

// RUDPIOHandler 클래스는 RIO(Registered I/O) 기반의 네트워크 통신을 처리하는 핸들러입니다.
// 세션 관리, 패킷 송수신, 재전송 처리 등 RUDP 프로토콜의 핵심 I/O 로직을 담당합니다.
class RUDPIOHandler : public IIOHandler
{
public:
	RUDPIOHandler(IRIOManager& inRioManager
		, ISessionDelegate& inSessionDelegate
		, CTLSMemoryPool<IOContext>& contextPool
		, std::vector<std::list<SendPacketInfo*>>& sendPacketInfoList
		, std::vector<std::unique_ptr<std::mutex>>& sendPacketInfoListLock
		, BYTE inMaxHoldingPacketQueueSize
		, unsigned int inRetransmissionMs
	);
	~RUDPIOHandler() override = default;

	RUDPIOHandler(const RUDPIOHandler&) = delete;
	RUDPIOHandler& operator=(const RUDPIOHandler&) = delete;
	RUDPIOHandler(RUDPIOHandler&&) = delete;
	RUDPIOHandler& operator=(RUDPIOHandler&&) = delete;

public:
	// IOCompleted 함수는 비동기 I/O 작업이 완료되었을 때 호출됩니다.
	// 전송된 바이트 수와 스레드 ID를 기반으로 후속 처리를 수행합니다.
	[[nodiscard]]
	bool IOCompleted(IOContext* context, ULONG transferred, BYTE threadId) const override;
	// DoRecv 함수는 주어진 세션으로부터 데이터를 수신하는 작업을 시작합니다.
	// RIO 수신 요청을 트리거하고 필요한 경우 버퍼를 관리합니다.
	[[nodiscard]]
	bool DoRecv(const RUDPSession& session) const override;
	// DoSend 함수는 주어진 세션에 데이터를 전송하는 작업을 시작합니다.
	// RIO 송신 요청을 트리거하고 전송 대기열을 관리합니다.
	[[nodiscard]]
	bool DoSend(OUT RUDPSession& session, ThreadIdType threadId) const override;

private:
	[[nodiscard]]
	bool RecvIOCompleted(OUT IOContext* contextResult, ULONG transferred, BYTE threadId) const;
	[[nodiscard]]
	bool SendIOCompleted(IOContext* context, BYTE threadId) const;

	[[nodiscard]]
	bool TryRIOSend(OUT RUDPSession& session, IOContext* context) const;
	[[nodiscard]]
	IOContext* MakeSendContext(OUT RUDPSession& session, ThreadIdType threadId) const;
	[[nodiscard]]
	unsigned int MakeSendStream(OUT RUDPSession& session, ThreadIdType threadId) const;

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
	std::vector<std::list<SendPacketInfo*>>& sendPacketInfoList;
	std::vector<std::unique_ptr<std::mutex>>& sendPacketInfoListLock;

	BYTE maximumHoldingPacketQueueSize;
	unsigned int retransmissionMs {};
};