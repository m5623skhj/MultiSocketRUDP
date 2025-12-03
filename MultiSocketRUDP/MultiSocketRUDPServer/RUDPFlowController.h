#pragma once
#include "CoreType.h"

class RUDPFlowController
{
public:
	explicit RUDPFlowController(BYTE inReceiveWindowSize);
	~RUDPFlowController() = default;

public:
	[[nodiscard]]
	PacketSequence GetReceiveWindowEnd(PacketSequence nextRecvSeq) const noexcept;

	[[nodiscard]]
	bool CanSendPacket(PacketSequence nextSendSequence, PacketSequence lastAckedSequence) const noexcept;

	void UpdateReceiverWindow(BYTE newReceiverWindowSize) noexcept;

	void OnReplyReceived(PacketSequence replySequence) noexcept;
	void OnCongestionEvent() noexcept;
	void OnTimeout() noexcept;

	[[nodiscard]]
	unsigned int GetCwnd() const noexcept { return cwnd; }
	[[nodiscard]]
	BYTE GetReceiverAdvertisedWindow() const noexcept { return receiverAdvertisedWindow; }
	[[nodiscard]]
	PacketSequence GetLastAckedSequence() const noexcept { return lastReplySequence; }

private:
	[[nodiscard]]
	BYTE GetEffectiveSendWindowSize() const noexcept;

private:
	BYTE receiveWindowSize{};
	BYTE receiverAdvertisedWindow{};
	BYTE cwnd{};
	PacketSequence lastReplySequence{};
	bool inRecovery{};

	static constexpr BYTE INITIAL_CWND = 4;
	static constexpr BYTE MAX_CWND = 255;

#ifdef _DEBUG
	unsigned int duplicateReplyCount{};
#endif
};
