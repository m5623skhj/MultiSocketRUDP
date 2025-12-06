#include "PreCompile.h"
#include "RUDPFlowController.h"
#include <algorithm>

RUDPFlowController::RUDPFlowController()
	: cwnd(INITIAL_CWND)
{
}

bool RUDPFlowController::CanSendPacket(const PacketSequence nextSendSequence, const PacketSequence lastAckedSequence) const noexcept
{
	const PacketSequence outstanding = nextSendSequence > lastAckedSequence ? nextSendSequence - lastAckedSequence : 0;
	return outstanding < static_cast<PacketSequence>(cwnd);
}

void RUDPFlowController::OnReplyReceived(const PacketSequence replySequence) noexcept
{
	static constexpr PacketSequence GAP_THRESHOLD = 5;

	if (replySequence <= lastReplySequence)
	{
#ifdef _DEBUG
		++duplicateReplyCount;
#endif
		return;
	}

	if (const PacketSequence sequenceGap = replySequence - lastReplySequence - 1; sequenceGap > GAP_THRESHOLD)
	{
		OnCongestionEvent();
		inRecovery = true;
	}

	lastReplySequence = replySequence;
	if (not inRecovery)
	{
		cwnd = min(cwnd + 1, MAX_CWND);
	}
	else
	{
		inRecovery = false;
	}
}

void RUDPFlowController::OnCongestionEvent() noexcept
{
	cwnd = max(cwnd / 2, 1u);
	inRecovery = true;
}

void RUDPFlowController::OnTimeout() noexcept
{
	cwnd = 1;
	inRecovery = true;
}
