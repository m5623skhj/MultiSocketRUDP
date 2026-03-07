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
	// ----------------------------------------
	// @brief 두 패킷 시퀀스 번호의 차이를 계산합니다.
	// 시퀀스 번호는 순환하므로, 직접적인 뺄셈이 아닌 흐름 제어 로직에 맞는
	// 상대적인 차이를 반환하여 순서가 뒤바뀌거나 누락된 패킷을 감지하는 데 사용됩니다.
	// @param a 첫 번째 패킷 시퀀스.
	// @param b 두 번째 패킷 시퀀스.
	// @return 두 시퀀스 번호의 차이.양수면 a가 b보다 나중 시퀀스임을 나타냅니다.
	// ----------------------------------------
	static int32_t SeqDiff(PacketSequence a, PacketSequence b) noexcept;

private:
	uint8_t cwnd{};
	PacketSequence lastReplySequence{};
	bool inRecovery{};

	static constexpr uint8_t INITIAL_CWND = 4;
	static constexpr uint8_t MAX_CWND = 250;

#ifdef _DEBUG
	uint8_t duplicateReplyCount{};
#endif
};
