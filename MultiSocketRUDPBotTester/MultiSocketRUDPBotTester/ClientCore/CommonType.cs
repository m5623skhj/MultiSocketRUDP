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
    INVALID_PACKET_ID = 0
    , PING = 1
    , PONG = 2
    , TEST_STRING_PACKET_REQ = 3
    , TEST_STRING_PACKET_RES = 4
    , TEST_PACKET_REQ = 5
    , TEST_PACKET_RES = 6
}

public enum PacketDirection : byte
{
    CLIENT_TO_SERVER = 0,
    CLIENT_TO_SERVER_REPLY = 1,
    SERVER_TO_CLIENT = 2,
    SERVER_TO_CLIENT_REPLY = 3,
    INVALID = 255
}