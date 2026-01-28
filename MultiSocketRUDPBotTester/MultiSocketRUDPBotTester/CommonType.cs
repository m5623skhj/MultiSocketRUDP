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
    InvalidPacketId = 0
    , Ping = 1
    , Pong = 2
    , TestStringPacketReq = 3
    , TestStringPacketRes = 4
    , TestPacketReq = 5
    , TestPacketRes = 6
}

public enum PacketDirection : byte
{
    ClientToServer = 0,
    ClientToServerReply = 1,
    ServerToClient = 2,
    ServerToClientReply = 3,
    Invalid = 255
}