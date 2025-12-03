#pragma once
#include "CoreType.h"

enum class CongestionState : BYTE
{
	SLOW_START = 0,
	CONGESTION_AVOIDANCE,
	FAST_RECOVERY,
};

class RUDPFlowController
{
public:
	RUDPFlowController() = delete;
	explicit RUDPFlowController(BYTE inReceiveWindowSize);

public:
	[[nodiscard]]
	PacketSequence GetReceiveWindowEnd(PacketSequence nextReceiveSequence) const;
	[[nodiscard]]
	BYTE GetSendWindowSize() const { return static_cast<BYTE>(sendWindowSize); }
	[[nodiscard]]
	BYTE GetEffectiveSendWindowSize() const;

	void OnAckReceived(PacketSequence ackedSequence);
	void OnTimeout();
	void OnDuplicateAck(PacketSequence duplicateSequence);

	void UpdateReceiverWindow(BYTE newReceiverWindowSize);

	[[nodiscard]]
	CongestionState GetCongestionState() const { return congestionState; }
	[[nodiscard]]
	unsigned int GetCongestionWindow() const { return cwnd; }
	[[nodiscard]]
	unsigned int GetSlowStartThreshold() const { return ssthresh; }

	[[nodiscard]]
	bool CanSendPacket(PacketSequence nextSendSequence, PacketSequence lastAckedSequence) const;

private:
	void EnterSlowStart();
	void EnterCongestionAvoidance();
	void EnterFastRecovery();

	void UpdateCongestionWindow();

private:
	BYTE receiveWindowSize{};
	BYTE receiverAdvertisedWindow{};

	unsigned int sendWindowSize{};
	unsigned int cwnd{ 1 };
	unsigned int ssthresh{ 65535 };

	CongestionState congestionState{ CongestionState::SLOW_START };
	unsigned int duplicateAckCount{};
	PacketSequence lastDuplicateAckSequence{};
	unsigned int ackCountInCongestionAvoidance{};

	static constexpr unsigned int INITIAL_CWND = 1;
	static constexpr unsigned int INITIAL_SSTHRESH = 65535;
	static constexpr unsigned int MAX_CWND = 255;
	static constexpr unsigned int DUPLICATE_ACK_THRESHOLD = 3;
};