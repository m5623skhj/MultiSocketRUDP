﻿global using PacketSequence = System.UInt64;
global using PacketRetransmissionCount = System.UInt16;
global using PacketId = System.UInt32;

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
    static public ulong GetNowMs()
    {
        return (ulong)DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
    }
}