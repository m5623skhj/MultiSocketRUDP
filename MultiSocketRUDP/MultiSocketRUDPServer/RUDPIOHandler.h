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
	[[nodiscard]]
	bool IOCompleted(IOContext* context, ULONG transferred, BYTE threadId) const override;
	[[nodiscard]]
	bool DoRecv(const RUDPSession& session) const override;
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