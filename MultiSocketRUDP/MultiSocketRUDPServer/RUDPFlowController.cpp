#include "PreCompile.h"
#include "RUDPFlowController.h"
#include <algorithm>

RUDPFlowController::RUDPFlowController(const BYTE inReceiveWindowSize)
	: receiveWindowSize(inReceiveWindowSize)
	, receiverAdvertisedWindow(inReceiveWindowSize)
	, sendWindowSize(inReceiveWindowSize)
{
}

PacketSequence RUDPFlowController::GetReceiveWindowEnd(const PacketSequence nextReceiveSequence) const
{
	return nextReceiveSequence + receiveWindowSize;
}
BYTE RUDPFlowController::GetEffectiveSendWindowSize() const
{
	const unsigned int effectiveWindow = min(cwnd, static_cast<unsigned int>(receiverAdvertisedWindow));
	return static_cast<BYTE>(min(effectiveWindow, static_cast<unsigned int>(MAX_CWND)));
}

void RUDPFlowController::OnAckReceived(const PacketSequence ackedSequence)
{
	if (ackedSequence != lastDuplicateAckSequence)
	{
		duplicateAckCount = 0;
		lastDuplicateAckSequence = ackedSequence;
	}

	switch (congestionState)
	{
	case CongestionState::SLOW_START:
	{
		cwnd = min(cwnd + 1, MAX_CWND);

		if (cwnd >= ssthresh)
		{
			EnterCongestionAvoidance();
		}
	}
	break;
	case CongestionState::CONGESTION_AVOIDANCE:
	{
		++ackCountInCongestionAvoidance;
		if (ackCountInCongestionAvoidance >= cwnd)
		{
			cwnd = min(cwnd + 1, MAX_CWND);
			ackCountInCongestionAvoidance = 0;
		}
	}
	break;
	case CongestionState::FAST_RECOVERY:
	{
		cwnd = ssthresh;
		EnterCongestionAvoidance();
	}
	break;
	}

	UpdateCongestionWindow();
}

void RUDPFlowController::OnTimeout()
{
	ssthresh = max(cwnd / 2, static_cast<unsigned int>(2));
	cwnd = INITIAL_CWND;
	EnterSlowStart();

	duplicateAckCount = 0;
}

void RUDPFlowController::OnDuplicateAck(const PacketSequence duplicateSequence)
{
	if (duplicateSequence == lastDuplicateAckSequence)
	{
		++duplicateAckCount;
	}
	else
	{
		duplicateAckCount = 1;
		lastDuplicateAckSequence = duplicateSequence;
	}

	if (duplicateAckCount >= DUPLICATE_ACK_THRESHOLD)
	{
		if (congestionState != CongestionState::FAST_RECOVERY)
		{
			ssthresh = max(cwnd / 2, static_cast<unsigned int>(2));
			cwnd = ssthresh + DUPLICATE_ACK_THRESHOLD;
			EnterFastRecovery();
		}
		else
		{
			cwnd = min(cwnd + 1, MAX_CWND);
		}
	}

	UpdateCongestionWindow();
}

void RUDPFlowController::UpdateReceiverWindow(const BYTE newReceiverWindowSize)
{
	receiverAdvertisedWindow = newReceiverWindowSize;
}

bool RUDPFlowController::CanSendPacket(const PacketSequence nextSendSequence, const PacketSequence lastAckedSequence) const
{
	const PacketSequence outstandingPackets = nextSendSequence - lastAckedSequence;
	return outstandingPackets < GetEffectiveSendWindowSize();
}

void RUDPFlowController::EnterSlowStart()
{
	congestionState = CongestionState::SLOW_START;
	ackCountInCongestionAvoidance = 0;
}

void RUDPFlowController::EnterCongestionAvoidance()
{
	congestionState = CongestionState::CONGESTION_AVOIDANCE;
	ackCountInCongestionAvoidance = 0;
}

void RUDPFlowController::EnterFastRecovery()
{
	congestionState = CongestionState::FAST_RECOVERY;
	ackCountInCongestionAvoidance = 0;
}

void RUDPFlowController::UpdateCongestionWindow()
{
	sendWindowSize = GetEffectiveSendWindowSize();
}