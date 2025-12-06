#pragma once
#include "../etc/CoreType.h"

class RUDPFlowController
{
public:
	explicit RUDPFlowController();
	~RUDPFlowController() = default;

public:
	bool CanSendPacket(PacketSequence nextSendSequence, PacketSequence lastAckedSequence) const noexcept;

	void OnReplyReceived(PacketSequence replySequence) noexcept;
	void OnCongestionEvent() noexcept;
	void OnTimeout() noexcept;

	BYTE GetCwnd() const noexcept { return cwnd; }
	PacketSequence GetLastAckedSequence() const noexcept { return lastReplySequence; }

private:
	BYTE cwnd{};
	PacketSequence lastReplySequence{};
	bool inRecovery{};

	static constexpr BYTE INITIAL_CWND = 4;
	static constexpr BYTE MAX_CWND = 255;

#ifdef _DEBUG
	BYTE duplicateReplyCount{};
#endif
};