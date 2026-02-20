#pragma once
#include "../etc/CoreType.h"
#include <vector>

class RUDPReceiveWindow
{
public:
	explicit RUDPReceiveWindow(BYTE recvWindowSize);
	~RUDPReceiveWindow() = default;

public:
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

private:
	static int32_t SeqDiff(PacketSequence a, PacketSequence b) noexcept;

private:
	PacketSequence windowStart{};
	BYTE windowSize{};
	std::vector<uint8_t> receivedFlags;
	size_t startIndex{};
};
