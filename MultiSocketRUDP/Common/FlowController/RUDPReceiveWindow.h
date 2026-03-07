#pragma once
#include "../etc/CoreType.h"
#include <vector>

class RUDPReceiveWindow
{
public:
	explicit RUDPReceiveWindow(BYTE recvWindowSize);
	~RUDPReceiveWindow() = default;

public:
	// ----------------------------------------
	// @brief 수신 윈도우의 크기를 조절하고, 수신 플래그 벡터를 새 크기로 재할당합니다.
	// @param recvWindowSize 새로운 수신 윈도우 크기
	// ----------------------------------------
	void ResizeRecvWindowSize(BYTE recvWindowSize);

	[[nodiscard]]
	bool CanReceive(PacketSequence inSequence) const noexcept;
	void MarkReceived(PacketSequence inSequence) noexcept;

	void Reset(PacketSequence startSequence) noexcept;

	[[nodiscard]]
	PacketSequence GetWindowStart() const noexcept { return windowStart; }
	[[nodiscard]]
	PacketSequence GetWindowEnd() const noexcept;
	[[nodiscard]]
	BYTE GetWindowSize() const noexcept { return windowSize; }
	[[nodiscard]]
	BYTE GetAdvertiseWindow() const noexcept;

private:
	static int32_t SeqDiff(PacketSequence a, PacketSequence b) noexcept;

private:
	PacketSequence windowStart{};
	BYTE windowSize{};
	std::vector<uint8_t> receivedFlags;
	BYTE usedCount{};
	size_t startIndex{};
};
