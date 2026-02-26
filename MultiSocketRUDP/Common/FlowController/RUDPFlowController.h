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
	uint8_t GetCwnd() const noexcept { return cwnd; }
	[[nodiscard]]
	PacketSequence GetLastAckedSequence() const noexcept { return lastReplySequence; }

private:
	static int32_t SeqDiff(PacketSequence a, PacketSequence b) noexcept;

private:
	uint8_t cwnd{};
	PacketSequence lastReplySequence{};
	bool inRecovery{};

	static constexpr uint8_t INITIAL_CWND = 4;
	static constexpr uint8_t MAX_CWND = 255;

#ifdef _DEBUG
	uint8_t duplicateReplyCount{};
#endif
};
