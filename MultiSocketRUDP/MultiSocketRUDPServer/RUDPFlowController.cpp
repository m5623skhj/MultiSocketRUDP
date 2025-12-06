#include "PreCompile.h"
#include "RUDPFlowController.h"
#include <algorithm>

RUDPFlowController::RUDPFlowController(const BYTE inReceiveWindowSize)
	: receiveWindowSize(inReceiveWindowSize)
	, receiverAdvertisedWindow(inReceiveWindowSize)
	, cwnd(INITIAL_CWND)
{
	if (cwnd == 0)
    {
		cwnd = 1;
    }
}

PacketSequence RUDPFlowController::GetReceiveWindowEnd(const PacketSequence nextRecvSeq) const noexcept
{
	return nextRecvSeq + static_cast<PacketSequence>(receiveWindowSize);
}

bool RUDPFlowController::CanSendPacket(const PacketSequence nextSendSequence, const PacketSequence lastAckedSequence) const noexcept
{
	const PacketSequence outstanding = nextSendSequence > lastAckedSequence ? nextSendSequence - lastAckedSequence : 0;
	return outstanding < static_cast<PacketSequence>(GetEffectiveSendWindowSize());
}

void RUDPFlowController::UpdateReceiverWindow(const BYTE newReceiverWindowSize) noexcept
{
	receiverAdvertisedWindow = newReceiverWindowSize;
}

void RUDPFlowController::OnReplyReceived(const PacketSequence replySequence) noexcept
{
	// GAP_THRESHOLD can be adjusted appropriately.
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
	cwnd = max(cwnd / 2, 1);
	inRecovery = true;
}

void RUDPFlowController::OnTimeout() noexcept
{
	cwnd = 1;
	inRecovery = true;
}

BYTE RUDPFlowController::GetEffectiveSendWindowSize() const noexcept
{
	const unsigned int effectiveWindow = std::min<unsigned int>(receiverAdvertisedWindow, cwnd);
	return static_cast<BYTE>(effectiveWindow);
}
