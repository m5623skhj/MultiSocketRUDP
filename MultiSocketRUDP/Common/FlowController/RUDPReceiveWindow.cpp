#include "PreCompile.h"
#include "RUDPReceiveWindow.h"

int32_t RUDPReceiveWindow::SeqDiff(PacketSequence a, PacketSequence b) noexcept
{
	return static_cast<int32_t>(a - b);
}

RUDPReceiveWindow::RUDPReceiveWindow(const BYTE recvWindowSize)
	: windowSize(recvWindowSize)
	, receivedFlags(recvWindowSize, 0)
{
}

bool RUDPReceiveWindow::CanReceive(const PacketSequence inSequence) const noexcept
{
	const int32_t diff = SeqDiff(inSequence, windowStart);
	return diff >= 0 && diff < windowSize;
}

void RUDPReceiveWindow::MarkReceived(const PacketSequence inSequence) noexcept
{
	if (!CanReceive(inSequence))
	{
		return;
	}

	const int32_t offset = SeqDiff(inSequence, windowStart);
	const size_t idx = (startIndex + static_cast<size_t>(offset)) % windowSize;
	receivedFlags[idx] = 1;

	while (receivedFlags[startIndex])
	{
		receivedFlags[startIndex] = 0;
		startIndex = (startIndex + 1) % windowSize;
		++windowStart;
	}
}

void RUDPReceiveWindow::Reset(const PacketSequence startSequence) noexcept
{
	windowStart = startSequence;
	startIndex = 0;
	std::fill(receivedFlags.begin(), receivedFlags.end(), 0);
}

PacketSequence RUDPReceiveWindow::GetWindowEnd() const noexcept
{
	return windowStart + windowSize;
}
