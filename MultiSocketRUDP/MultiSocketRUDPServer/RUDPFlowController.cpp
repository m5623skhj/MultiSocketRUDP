#include "PreCompile.h"
#include "RUDPFlowController.h"

RUDPFlowController::RUDPFlowController(const BYTE inReceiveWindowSize)
	: receiveWindowSize(inReceiveWindowSize)
{
}

PacketSequence RUDPFlowController::GetReceiveWindowEnd(const PacketSequence nextReceiveSequence) const
{
	return nextReceiveSequence + receiveWindowSize;
}
