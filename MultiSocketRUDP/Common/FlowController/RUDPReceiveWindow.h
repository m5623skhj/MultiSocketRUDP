#pragma once
#include "../etc/CoreType.h"
#include <vector>

class RUDPReceiveWindow
{
public:
	explicit RUDPReceiveWindow(BYTE recvWindowSize);
	~RUDPReceiveWindow() = default;

public:
	[[nodiscard]]
	bool CanReceive(PacketSequence inSequence) const noexcept;
	void MarkReceived(PacketSequence inSequence) noexcept;

	[[nodiscard]]
	PacketSequence GetWindowStart() const noexcept { return windowStart; }
	[[nodiscard]]
	PacketSequence GetWindowEnd() const noexcept;
	[[nodiscard]]
	BYTE GetWindowSize() const noexcept { return windowSize; }

private:
	PacketSequence windowStart{};
	BYTE windowSize{};
	std::vector<bool> receivedFlags;
	size_t startIndex{};
};