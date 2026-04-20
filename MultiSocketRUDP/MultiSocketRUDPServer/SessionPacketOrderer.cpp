#include "PreCompile.h"
#include "SessionPacketOrderer.h"
#include "../Logger/Logger.h"
#include "LogExtension.h"

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

	if (sequence < expected)
	{
		return ON_RECV_RESULT::DUPLICATED_RECV;
	}

	if (sequence == expected)
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

	if (not recvHoldingPacketSequences.contains(sequence) )
	{
		if (recvHoldingPacketSequences.size() >= maxHoldingQueueSize)
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
		recvPacketHolderQueue.emplace(&buffer, sequence);
		recvHoldingPacketSequences.emplace(sequence);
	}

	return ON_RECV_RESULT::PACKET_HELD;
}

void SessionPacketOrderer::Reset(const PacketSequence startSequence)
{
	nextRecvPacketSequence.store(startSequence, std::memory_order_relaxed);

	while (not recvPacketHolderQueue.empty())
	{
		NetBuffer::Free(recvPacketHolderQueue.top().buffer);
		recvPacketHolderQueue.pop();
	}

	recvHoldingPacketSequences.clear();
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

	recvHoldingPacketSequences.erase(sequence);
	return true;
}

bool SessionPacketOrderer::ProcessHoldingPacket(const PacketProcessCallback& callback)
{
	while (not recvPacketHolderQueue.empty())
	{
		const PacketSequence expected = nextRecvPacketSequence.load(std::memory_order_relaxed);
		auto& top = recvPacketHolderQueue.top();

		if (top.packetSequence > expected)
		{
			break;
		}

		const PacketSequence topSequence = top.packetSequence;
		NetBuffer* storedBuffer = top.buffer;
		recvPacketHolderQueue.pop();

		if (topSequence < expected)
		{
			recvHoldingPacketSequences.erase(topSequence);
			NetBuffer::Free(storedBuffer);
			continue;
		}

		if (not ProcessAndAdvance(*storedBuffer, topSequence, callback))
		{
			NetBuffer::Free(storedBuffer);
			return false;
		}

		NetBuffer::Free(storedBuffer);
	}

	return true;
}
