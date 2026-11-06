#pragma once
#include "PrimitiveTypes.h"

#include <bitset>

// Receive-worker owned. Call only after authentication; reset for every new session.
// Sequence exhaustion/wrap is intentionally outside protocol v2's supported lifetime.
class LatestPacketSequence
{
public:
    bool Accept(const PacketSequence sequence) noexcept
    {
        if (hasReceived && sequence <= latest)
        {
            return false;
        }

        latest = sequence;
        hasReceived = true;
        return true;
    }
    void Reset() noexcept { latest = 0; hasReceived = false; }

private:
    PacketSequence latest{};
    bool hasReceived{};
};

// The authenticated client window covers two 64-chunk snapshots and interleaved damage packets.
// Call Reset when the session is replaced. Other packet families remain newest-only.
class ReorderedUnreliablePacketSequence
{
public:
    bool Accept(const PacketSequence sequence, const bool allowReordering = false) noexcept
    {
        if (sequence == 0) return false;
        if (sequence > latest)
        {
            const auto distance = sequence - latest;
            if (distance >= received.size()) received.reset();
            else received <<= static_cast<std::size_t>(distance);
            received.set(0);
            latest = sequence;
            return true;
        }
        const auto distance = latest - sequence;
        if (!allowReordering || distance >= received.size() || received[static_cast<std::size_t>(distance)])
            return false;
        received.set(static_cast<std::size_t>(distance));
        return true;
    }

    void Reset() noexcept { latest = 0; received.reset(); }

private:
    PacketSequence latest{};
    std::bitset<192> received;
};
