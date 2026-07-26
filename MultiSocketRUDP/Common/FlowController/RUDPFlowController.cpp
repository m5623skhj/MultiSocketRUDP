#include "PreCompile.h"
#include "RUDPFlowController.h"
#include <algorithm>

int64_t RUDPFlowController::SeqDiff(const PacketSequence a, const PacketSequence b) noexcept
{
	return static_cast<int64_t>(a - b);
}

RUDPFlowController::RUDPFlowController()
	: cwnd(INITIAL_CWND)
{
}

bool RUDPFlowController::CanSendPacket(const PacketSequence nextSendSequence, const PacketSequence lastAckedSequence) const noexcept
{
	const int64_t diff = SeqDiff(nextSendSequence, lastAckedSequence);
	const int64_t outstanding = diff > 1 ? diff - 1 : 0;
	return outstanding < cwnd;
}

void RUDPFlowController::OnReplyReceived(const PacketSequence replySequence) noexcept
{
	static constexpr int32_t GAP_THRESHOLD = 5;

	const int64_t diff = SeqDiff(replySequence, lastReplySequence);
	if (diff <= 0)
	{
#ifdef _DEBUG
		++duplicateReplyCount;
#endif
		return;
	}

	if (const int64_t sequenceGap = diff - 1; sequenceGap >= GAP_THRESHOLD)
	{
		OnCongestionEvent();
	}

	lastReplySequence = replySequence;

	if (not inRecovery)
	{
		cwnd = std::min<uint8_t>(cwnd + 1, MAX_CWND);
	}
	else
	{
		inRecovery = false;
	}
}

void RUDPFlowController::OnCongestionEvent() noexcept
{
	cwnd = std::max<uint8_t>(cwnd / 2, 1);
	inRecovery = true;
}

void RUDPFlowController::OnTimeout() noexcept
{
	cwnd = std::max<uint8_t>(cwnd / 2, 1);
	inRecovery = true;
}

void RUDPFlowController::Reset() noexcept
{
	cwnd = INITIAL_CWND;
	lastReplySequence = 0;
	inRecovery = false;
#ifdef _DEBUG
	duplicateReplyCount = 0;
#endif
}
