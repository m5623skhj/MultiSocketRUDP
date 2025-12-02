#pragma once
#include "CoreType.h"

class RUDPFlowController
{
public:
	RUDPFlowController() = delete;
	explicit RUDPFlowController(BYTE inReceiveWindowSize);

public:
	[[nodiscard]]
	PacketSequence GetReceiveWindowEnd(PacketSequence nextReceiveSequence) const;

private:
	BYTE receiveWindowSize{};
};
