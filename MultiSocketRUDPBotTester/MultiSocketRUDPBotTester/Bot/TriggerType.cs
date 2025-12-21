using MultiSocketRUDPBotTester.Buffer;

namespace MultiSocketRUDPBotTester.Bot
{
    public enum TriggerType
    {
        OnConnected,
        OnDisconnected,
        OnPacketReceived,
        Manual
    }

    public class TriggerCondition
    {
        public TriggerType Type { get; set; }
        public PacketId? PacketId { get; set; }
        public Func<NetBuffer, bool>? PacketValidator { get; set; }

        public bool Matches(TriggerType type, PacketId? packetId = null, NetBuffer? buffer = null)
        {
            if (Type != type)
            {
                return false;
            }

            if (Type != TriggerType.OnPacketReceived)
            {
                return true;
            }

            if (PacketId.HasValue && PacketId != packetId)
            {
                return false;
            }

            if (PacketValidator != null && buffer != null)
            {
                return PacketValidator(buffer);
            }

            return true;
        }
    }
}