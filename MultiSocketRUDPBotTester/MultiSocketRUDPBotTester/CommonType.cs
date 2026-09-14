global using PacketSequence = ulong;
global using PacketRetransmissionCount = ushort;

public enum PacketType : byte
{
    InvalidType = 0,
    ConnectType = 1,
    DisconnectType = 2,
    SendType = 3,
    SendReplyType = 4,
    HeartbeatType = 5,
    HeartbeatReplyType = 6,
    UnreliableSendType = 7,
}

public static class CommonFunc
{
    public static ulong GetNowMs()
    {
        return (ulong)DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
    }
}

public enum PacketDirection : byte
{
    ClientToServer = 0,
    ClientToServerReply = 1,
    ServerToClient = 2,
    ServerToClientReply = 3,
    ClientToServerUnreliable = 4,
    ServerToClientUnreliable = 5,
    Invalid = 255
}

public static class ProtocolConstants
{
    public const uint Version = 2;
    public const int UnreliableQueueCapacity = 64;
}
