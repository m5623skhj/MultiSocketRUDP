#include "PreCompile.h"
#include "RUDPReceiveWindow.h"

RUDPReceiveWindow::RUDPReceiveWindow(const BYTE recvWindowSize)
	: windowSize(recvWindowSize)
	, receivedFlags(recvWindowSize, false)
{
}

bool RUDPReceiveWindow::CanReceive(const PacketSequence inSequence) const noexcept
{
	return (inSequence - windowStart) < windowSize;
}

void RUDPReceiveWindow::MarkReceived(const PacketSequence inSequence) noexcept
{
	if (not CanReceive(inSequence))
	{
		return;
	}

	const PacketSequence offset = inSequence - windowStart;
	receivedFlags[offset] = true;

	while (not receivedFlags.empty() && receivedFlags.front())
	{
		receivedFlags[startIndex] = false;
		startIndex = (startIndex + 1) % windowSize;
		++windowStart;
	}
}

PacketSequence RUDPReceiveWindow::GetWindowEnd() const noexcept
{
	return windowStart + windowSize;
}