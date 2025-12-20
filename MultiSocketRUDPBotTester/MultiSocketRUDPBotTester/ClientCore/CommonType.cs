global using PacketSequence = ulong;
global using PacketRetransmissionCount = ushort;

public enum PacketType : byte
{
    INVALID_TYPE = 0,
    CONNECT_TYPE = 1,
    DISCONNECT_TYPE = 2,
    SEND_TYPE = 3,
    SEND_REPLY_TYPE = 4,
    HEARTBEAT_TYPE = 5,
    HEARTBEAT_REPLY_TYPE = 6,
}

public static class CommonFunc
{
    public static ulong GetNowMs()
    {
        return (ulong)DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
    }
}

public enum PacketId : uint
{

}