using MultiSocketRUDPBotTester.Buffer;

namespace MultiSocketRUDPBotTester.ClientCore
{
    internal sealed class ReceivePacketOrderer
    {
        private const ulong HalfSequenceRange = 1UL << 63;

        private readonly Dictionary<PacketSequence, HeldPacket> heldPackets = [];
        private readonly Lock orderLock = new();
        private PacketSequence expectedSequence;

        public List<(PacketSequence, PacketId, NetBuffer, PacketType)> Collect(
            PacketSequence packetSequence,
            PacketId packetId,
            NetBuffer buffer,
            PacketType packetType)
        {
            var result = new List<(PacketSequence, PacketId, NetBuffer, PacketType)>();
            lock (orderLock)
            {
                if (!IsNewer(packetSequence, expectedSequence))
                {
                    return result;
                }

                if (packetSequence != Next(expectedSequence))
                {
                    heldPackets.TryAdd(packetSequence, new HeldPacket
                    {
                        Sequence = packetSequence,
                        PacketId = packetId,
                        Buffer = buffer,
                        PacketType = packetType
                    });
                    return result;
                }

                expectedSequence = packetSequence;
                result.Add((packetSequence, packetId, buffer, packetType));

                while (heldPackets.Remove(Next(expectedSequence), out var held))
                {
                    expectedSequence = held.Sequence;
                    result.Add((held.Sequence, held.PacketId, held.Buffer, held.PacketType));
                }
            }

            return result;
        }

        public PacketSequence GetExpectedSequence()
        {
            lock (orderLock)
            {
                return expectedSequence;
            }
        }

        internal int GetHeldPacketCount()
        {
            lock (orderLock)
            {
                return heldPackets.Count;
            }
        }

        public void Clear()
        {
            lock (orderLock)
            {
                expectedSequence = 0;
                heldPackets.Clear();
            }
        }

        internal void SetExpectedSequenceForTest(PacketSequence sequence)
        {
            lock (orderLock)
            {
                expectedSequence = sequence;
                heldPackets.Clear();
            }
        }

        private static bool IsNewer(PacketSequence candidate, PacketSequence reference)
        {
            var distance = unchecked(candidate - reference);
            return distance != 0 && distance < HalfSequenceRange;
        }

        private static PacketSequence Next(PacketSequence sequence) => unchecked(sequence + 1);
    }
}
