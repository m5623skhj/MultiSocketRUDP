#pragma once
#include <queue>
#include <functional>
#include <atomic>
#include <unordered_set>

#include "NetServerSerializeBuffer.h"
#include "../Common/etc/CoreType.h"

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

enum class ON_RECV_RESULT : uint8_t
{
	PROCESSED = 0,
	DUPLICATED_RECV,
	PACKET_HELD,
	ERROR_OCCURED,
};

class SessionPacketOrderer
{
public:
	using PacketProcessCallback = std::function<bool(NetBuffer&, PacketSequence)>;

	explicit SessionPacketOrderer(BYTE inMaxHoldingQueueSize);
	~SessionPacketOrderer();

	SessionPacketOrderer(const SessionPacketOrderer&) = delete;
	SessionPacketOrderer(const SessionPacketOrderer&&) = delete;
	SessionPacketOrderer& operator=(const SessionPacketOrderer&) = delete;
	SessionPacketOrderer& operator=(const SessionPacketOrderer&&) = delete;

public:
	void Initialize(BYTE inMaxHoldingQueueSize);

	void SetMaximumHoldingQueueSize(BYTE inMaxHoldingQueueSize);

	[[nodiscard]]
	ON_RECV_RESULT OnReceive(PacketSequence sequence, NetBuffer& buffer, const PacketProcessCallback& callback);
	void Reset(PacketSequence startSequence);

	[[nodiscard]]
	PacketSequence GetNextExpected() const noexcept;

private:
	[[nodiscard]]
	bool ProcessAndAdvance(NetBuffer& buffer, PacketSequence sequence, const PacketProcessCallback& callback);
	[[nodiscard]]
	bool ProcessHoldingPacket(const PacketProcessCallback& callback);

	struct RecvPacketInfoPriority
	{
		bool operator()(const RecvPacketInfo& lhs, const RecvPacketInfo& rhs) const
		{
			return lhs.packetSequence > rhs.packetSequence;
		}
	};

	std::atomic<PacketSequence> nextRecvPacketSequence{};
	std::priority_queue<RecvPacketInfo, std::vector<RecvPacketInfo>, RecvPacketInfoPriority> recvPacketHolderQueue;
	std::unordered_set<PacketSequence> recvHoldingPacketSequences;

	BYTE maxHoldingQueueSize{};
};