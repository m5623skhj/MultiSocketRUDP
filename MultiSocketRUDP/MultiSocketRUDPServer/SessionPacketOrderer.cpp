#include "PreCompile.h"
#include "SessionPacketOrderer.h"
#include "../Logger/Logger.h"
#include "LogExtension.h"
#include <ranges>

SessionPacketOrderer::SessionPacketOrderer(const BYTE inMaxHoldingQueueSize)
	: maxHoldingQueueSize(inMaxHoldingQueueSize)
{
}

SessionPacketOrderer::~SessionPacketOrderer()
{
	Reset(0);
}

void SessionPacketOrderer::Initialize(const BYTE inMaxHoldingQueueSize)
{
	Reset(0);
	SetMaximumHoldingQueueSize(inMaxHoldingQueueSize);
}

void SessionPacketOrderer::SetMaximumHoldingQueueSize(const BYTE inMaxHoldingQueueSize)
{
	maxHoldingQueueSize = inMaxHoldingQueueSize;
}

ON_RECV_RESULT SessionPacketOrderer::OnReceive(const PacketSequence sequence, NetBuffer& buffer, const PacketProcessCallback& callback)
{
	const PacketSequence expected = nextRecvPacketSequence.load(std::memory_order_relaxed);
	const int64_t sequenceDiff = static_cast<int64_t>(sequence - expected);

	if (sequenceDiff < 0)
	{
		return ON_RECV_RESULT::DUPLICATED_RECV;
	}

	if (sequenceDiff == 0)
	{
		if (not ProcessAndAdvance(buffer, sequence, callback))
		{
			return ON_RECV_RESULT::ERROR_OCCURED;
		}

		if (not ProcessHoldingPacket(callback))
		{
			return ON_RECV_RESULT::ERROR_OCCURED;
		}

		return ON_RECV_RESULT::PROCESSED;
	}

	if (not recvHoldingPackets.contains(sequence))
	{
		if (recvHoldingPackets.size() >= maxHoldingQueueSize)
		{
			LOG_ERROR(std::format(
				"SessionPacketOrderer: holding queue full. "
				"maxHoldingQueueSize={}, nextExpected={}, lostSeq={} → DoDisconnect",
				maxHoldingQueueSize,
				nextRecvPacketSequence.load(std::memory_order_relaxed),
				sequence));

			return ON_RECV_RESULT::ERROR_OCCURED;
		}

		NetBuffer::AddRefCount(&buffer);
		recvHoldingPackets.emplace(sequence, &buffer);
	}

	return ON_RECV_RESULT::PACKET_HELD;
}

void SessionPacketOrderer::Reset(const PacketSequence startSequence)
{
	nextRecvPacketSequence.store(startSequence, std::memory_order_relaxed);

	for (const auto& buffer : recvHoldingPackets | std::views::values)
	{
		NetBuffer::Free(buffer);
	}
	recvHoldingPackets.clear();
}

PacketSequence SessionPacketOrderer::GetNextExpected() const noexcept
{
	return nextRecvPacketSequence;
}

bool SessionPacketOrderer::ProcessAndAdvance(NetBuffer& buffer, const PacketSequence sequence, const PacketProcessCallback& callback)
{
	nextRecvPacketSequence.fetch_add(1, std::memory_order_relaxed);
	if (not callback(buffer, sequence))
	{
		return false;
	}

	recvHoldingPackets.erase(sequence);
	return true;
}

bool SessionPacketOrderer::ProcessHoldingPacket(const PacketProcessCallback& callback)
{
	while (true)
	{
		const PacketSequence expected = nextRecvPacketSequence.load(std::memory_order_relaxed);
		const auto heldPacket = recvHoldingPackets.find(expected);
		if (heldPacket == recvHoldingPackets.end())
		{
			break;
		}

		NetBuffer* storedBuffer = heldPacket->second;
		recvHoldingPackets.erase(heldPacket);

		if (not ProcessAndAdvance(*storedBuffer, expected, callback))
		{
			NetBuffer::Free(storedBuffer);
			return false;
		}

		NetBuffer::Free(storedBuffer);
	}

	return true;
}
