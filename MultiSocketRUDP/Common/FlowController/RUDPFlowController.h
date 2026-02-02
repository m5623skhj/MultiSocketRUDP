#pragma once
#include "../etc/CoreType.h"

class RUDPFlowController
{
public:
	explicit RUDPFlowController();
	~RUDPFlowController() = default;

public:
	[[nodiscard]]
	bool CanSendPacket(PacketSequence nextSendSequence, PacketSequence lastAckedSequence) const noexcept;

	void OnReplyReceived(PacketSequence replySequence) noexcept;
	void OnCongestionEvent() noexcept;
	void OnTimeout() noexcept;

	void Reset() noexcept;

	[[nodiscard]]
	uint16_t GetCwnd() const noexcept { return cwnd; }
	[[nodiscard]]
	PacketSequence GetLastAckedSequence() const noexcept { return lastReplySequence; }

private:
	static int32_t SeqDiff(PacketSequence a, PacketSequence b) noexcept;

private:
	uint16_t cwnd{};
	PacketSequence lastReplySequence{};
	bool inRecovery{};

	static constexpr uint16_t INITIAL_CWND = 4;
	static constexpr uint16_t MAX_CWND = 1024;

#ifdef _DEBUG
	uint16_t duplicateReplyCount{};
#endif
};
