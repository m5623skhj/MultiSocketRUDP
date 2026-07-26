using System.Security.Cryptography;
using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.ClientCore;

namespace MultiSocketRUDPBotTester.UnitTests;

public sealed class NetBufferBoundaryTests
{
    private static readonly byte[] Key = Enumerable.Range(1, 16).Select(value => (byte)value).ToArray();
    private static readonly byte[] Salt = Enumerable.Range(21, 16).Select(value => (byte)value).ToArray();

    [Fact]
    public void ReadsAndSkipsCannotCrossWrittenData()
    {
        var buffer = new NetBuffer(16);
        buffer.WriteByte(1);
        Assert.Equal(1, buffer.ReadByte());

        Assert.Throws<InvalidOperationException>(() => buffer.ReadByte());
        Assert.Throws<InvalidOperationException>(() => buffer.ReadUShort());
        Assert.Throws<InvalidOperationException>(() => buffer.ReadUInt());
        Assert.Throws<InvalidOperationException>(() => buffer.ReadULong());
        Assert.Throws<ArgumentOutOfRangeException>(() => buffer.SkipBytes(-1));
        Assert.Throws<InvalidOperationException>(() => buffer.SkipBytes(1));
    }

    [Fact]
    public void WritesAndMetadataInsertionCannotCrossCapacityOrMissingHeader()
    {
        var full = new NetBuffer(1);
        full.WriteByte(1);
        Assert.Throws<InvalidOperationException>(() => full.WriteByte(2));
        Assert.Throws<ArgumentException>(() => new NetBuffer(8).WriteBytes([1, 2], 1, 2));

        var noHeader = new NetBuffer(32);
        Assert.Throws<InvalidOperationException>(() => noHeader.InsertPacketType(PacketType.SendType));
        Assert.Throws<InvalidOperationException>(() => new NetBuffer(4).ReserveHeader());

        var incompletePacket = new NetBuffer(32);
        incompletePacket.ReserveHeader();
        using var aes = new AesGcm(Key, CryptoHelper.AuthTagSize);
        Assert.Throws<InvalidOperationException>(
            () => NetBuffer.EncodePacket(
                aes,
                incompletePacket,
                0,
                PacketDirection.ClientToServer,
                Salt,
                isCorePacket: false));
    }

    [Fact]
    public void Utf8StringAcceptsUshortMaximumAndRejectsLongerPayload()
    {
        var maximum = new string('a', ushort.MaxValue);
        var buffer = new NetBuffer(sizeof(ushort) + ushort.MaxValue);
        buffer.WriteString(maximum);
        Assert.Equal(maximum, buffer.ReadString());

        var tooLong = new string('a', ushort.MaxValue + 1);
        Assert.Throws<ArgumentOutOfRangeException>(
            () => new NetBuffer(sizeof(ushort) + tooLong.Length).WriteString(tooLong));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(5)]
    [InlineData(13)]
    [InlineData(14)]
    [InlineData(29)]
    public void DecodeReportsInvalidLayoutForEveryShortFullPacket(int length)
    {
        var packet = FromBytes(new byte[length]);
        using var aes = new AesGcm(Key, CryptoHelper.AuthTagSize);

        var decoded = NetBuffer.DecodePacket(
            aes,
            packet,
            isCorePacket: false,
            Salt,
            PacketDirection.ServerToClient,
            out var failure);

        Assert.False(decoded);
        Assert.Equal(DecodePacketFailureReason.InvalidLayout, failure.Reason);
        Assert.Equal(length, failure.PacketLength);
    }

    [Fact]
    public void DecodeRejectsHeaderPayloadLengthThatDoesNotMatchPacketLength()
    {
        var packet = new NetBuffer(64);
        packet.ReserveHeader();
        packet.WriteByte(7);
        packet.InsertPacketType(PacketType.SendType);
        packet.InsertPacketSequence(3);
        packet.InsertPacketId(PacketId.TestPacketReq);
        using var aes = new AesGcm(Key, CryptoHelper.AuthTagSize);
        NetBuffer.EncodePacket(
            aes,
            packet,
            3,
            PacketDirection.ServerToClient,
            Salt,
            isCorePacket: false);
        var bytes = packet.GetPacketBuffer();
        bytes[1]--;

        var malformed = FromBytes(bytes);
        Assert.False(NetBuffer.DecodePacket(
            aes,
            malformed,
            isCorePacket: false,
            Salt,
            PacketDirection.ServerToClient,
            out var failure));
        Assert.Equal(DecodePacketFailureReason.InvalidLayout, failure.Reason);
    }

    private static NetBuffer FromBytes(byte[] bytes)
    {
        var buffer = new NetBuffer(bytes.Length);
        buffer.WriteBytes(bytes);
        return buffer;
    }
}
