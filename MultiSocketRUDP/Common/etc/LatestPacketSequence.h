#pragma once
#include "PrimitiveTypes.h"

// Receive-worker owned. Call only after authentication; reset for every new session.
// Sequence exhaustion/wrap is intentionally outside protocol v2's supported lifetime.
class LatestPacketSequence
{
public:
    bool Accept(const PacketSequence sequence) noexcept
    {
        if (hasReceived && sequence <= latest) return false;
        latest = sequence;
        hasReceived = true;
        return true;
    }
    void Reset() noexcept { latest = 0; hasReceived = false; }
private:
    PacketSequence latest{};
    bool hasReceived{};
};
