#include "PreCompile.h"
#include "RUDPReceiveWindow.h"

RUDPReceiveWindow::RUDPReceiveWindow(const BYTE recvWindowSize)
    : windowSize(recvWindowSize)
	, receivedFlags(recvWindowSize, false)
{
}

bool RUDPReceiveWindow::CanReceive(const PacketSequence inSequence) const noexcept
{
    return inSequence >= windowStart && inSequence < windowStart + windowSize;
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
        receivedFlags.erase(receivedFlags.begin());
        receivedFlags.push_back(false);
        ++windowStart;
    }
}

PacketSequence RUDPReceiveWindow::GetWindowEnd(const PacketSequence nextRecv) const noexcept
{
    return nextRecv + static_cast<PacketSequence>(windowSize);
}