#pragma once
#include "../etc/CoreType.h"
#include <vector>

class RUDPReceiveWindow
{
public:
    explicit RUDPReceiveWindow(BYTE recvWindowSize);
    ~RUDPReceiveWindow() = default;

public:
    bool CanReceive(PacketSequence inSequence) const noexcept;
    void MarkReceived(PacketSequence inSequence) noexcept;

    PacketSequence GetWindowStart() const noexcept { return windowStart; }
    PacketSequence GetWindowEnd(PacketSequence nextRecv) const noexcept;
    BYTE GetWindowSize() const noexcept { return windowSize; }

private:
    PacketSequence windowStart{};
    BYTE windowSize{};
    std::vector<bool> receivedFlags;
};