using System.Security.Cryptography;
using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.ClientCore;

namespace MultiSocketRUDPBotTester.UnitTests;

public sealed class NetBufferCryptoTests
{
    private static readonly byte[] Key = Enumerable.Range(1, 16).Select(i => (byte)i).ToArray();
    private static readonly byte[] Salt = Enumerable.Range(0xA0, 16).Select(i => (byte)i).ToArray();

    /// <summary>
    /// 기본형 값이 little-endian 바이트 순서를 유지하며 쓰기와 읽기 사이에서 왕복하는지 확인합니다.
    /// </summary>
    [Fact]
    public void PrimitiveValuesRoundTripInLittleEndianOrder()
    {
        var buffer = new NetBuffer(128);
        buffer.WriteByte(0xAB);
        buffer.WriteUShort(0x1234);
        buffer.WriteUInt(0x89ABCDEF);
        buffer.WriteInt(unchecked((int)0xFEDCBA98));
        buffer.WriteULong(0x0102030405060708UL);

        Assert.Equal(
            new byte[]
            {
                0xAB, 0x34, 0x12, 0xEF, 0xCD, 0xAB, 0x89,
                0x98, 0xBA, 0xDC, 0xFE, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01
            },
            buffer.GetPacketBuffer());
        Assert.Equal(0xAB, buffer.ReadByte());
        Assert.Equal(0x1234, buffer.ReadUShort());
        Assert.Equal(0x89ABCDEFU, buffer.ReadUInt());
        Assert.Equal(unchecked((int)0xFEDCBA98), buffer.ReadInt());
        Assert.Equal(0x0102030405060708UL, buffer.ReadULong());
    }

    /// <summary>
    /// 빈 문자열, ASCII 및 UTF-8 문자열이 길이 정보와 함께 손실 없이 왕복하는지 확인합니다.
    /// </summary>
    [Theory]
    [InlineData("")]
    [InlineData("hello")]
    [InlineData("한글🙂")]
    public void StringRoundTripsAsUtf8(string value)
    {
        var buffer = new NetBuffer(128);
        buffer.WriteString(value);

        Assert.Equal(value, buffer.ReadString());
    }

    /// <summary>
    /// 범위를 지정한 바이트 쓰기가 요청한 구간만 복사하고 동일한 데이터로 읽히는지 확인합니다.
    /// </summary>
    [Fact]
    public void WriteBytesWithRangeCopiesOnlyRequestedSegment()
    {
        var buffer = new NetBuffer(16);
        buffer.WriteBytes([0, 1, 2, 3, 4], 1, 3);

        Assert.Equal(new byte[] { 1, 2, 3 }, buffer.GetPacketBuffer());
        Assert.Equal(new byte[] { 1, 2, 3 }, buffer.ReadBytes(3));
    }

    /// <summary>
    /// 패킷 타입, 시퀀스 및 ID 삽입이 암호화 전 와이어 레이아웃을 올바르게 구성하는지 확인합니다.
    /// </summary>
    [Fact]
    public void InsertMetadataBuildsExpectedUnencryptedLayout()
    {
        var buffer = new NetBuffer(64);
        buffer.ReserveHeader();
        buffer.WriteBytes([0xDE, 0xAD]);
        buffer.InsertPacketType(PacketType.SendType);
        buffer.InsertPacketSequence(0x0102030405060708UL);
        buffer.InsertPacketId((PacketId)0x11223344U);

        Assert.Equal(
            new byte[]
            {
                0, 0, 0, 0, 0,
                (byte)PacketType.SendType,
                8, 7, 6, 5, 4, 3, 2, 1,
                0x44, 0x33, 0x22, 0x11,
                0xDE, 0xAD
            },
            buffer.GetPacketBuffer());
    }

    /// <summary>
    /// CONNECT 및 Core 패킷 빌더가 기존 상태를 초기화하고 예상 payload를 구성하는지 확인합니다.
    /// </summary>
    [Fact]
    public void ConnectAndCoreBuildersResetAndBuildExpectedPayload()
    {
        var buffer = new NetBuffer(64);
        buffer.BuildConnectPacket(0x1234);
        Assert.Equal(
            new byte[] { 0, 0, 0, 0, 0, (byte)PacketType.ConnectType, 0, 0, 0, 0, 0, 0, 0, 0, 0x34, 0x12 },
            buffer.GetPacketBuffer());

        buffer = new NetBuffer(64);
        buffer.BuildCorePacket(PacketType.HeartbeatType, 0x0102030405060708UL);
        Assert.Equal(
            new byte[] { 0, 0, 0, 0, 0, (byte)PacketType.HeartbeatType, 8, 7, 6, 5, 4, 3, 2, 1 },
            buffer.GetPacketBuffer());
    }

    /// <summary>
    /// Core/Full 패킷과 네 방향 조합에서 본문이 암호화·복호화되고 인증 태그가 제거되는지 확인합니다.
    /// </summary>
    [Theory]
    [InlineData(false, PacketDirection.ClientToServer)]
    [InlineData(false, PacketDirection.ServerToClient)]
    [InlineData(true, PacketDirection.ClientToServerReply)]
    [InlineData(true, PacketDirection.ServerToClientReply)]
    public void EncodeDecodeRoundTripsBodyAndRemovesAuthenticationTag(bool isCorePacket, PacketDirection direction)
    {
        const ulong sequence = 0x0102030405060708UL;
        var originalHeaderCode = NetBuffer.HeaderCode;
        try
        {
            NetBuffer.HeaderCode = 0xCC;
            var packet = BuildPacket(isCorePacket, sequence, [0xDE, 0xAD, 0xBE, 0xEF]);
            var unencodedLength = packet.GetLength();
            using var aes = new AesGcm(Key, CryptoHelper.AuthTagSize);

            NetBuffer.EncodePacket(aes, packet, sequence, direction, Salt, isCorePacket);

            Assert.Equal(unencodedLength + CryptoHelper.AuthTagSize, packet.GetLength());
            Assert.Equal(0xCC, packet.GetPacketBuffer()[0]);
            Assert.True(NetBuffer.DecodePacket(aes, packet, isCorePacket, Salt, direction, out var failure));
            Assert.Equal(default, failure);
            Assert.Equal(unencodedLength, packet.GetLength());
            Assert.Equal(
                new byte[] { 0xDE, 0xAD, 0xBE, 0xEF },
                packet.GetPacketBuffer()[^4..]);
        }
        finally
        {
            NetBuffer.HeaderCode = originalHeaderCode;
        }
    }

    /// <summary>
    /// AAD, ciphertext 또는 인증 태그 변조가 AES-GCM 인증 실패로 거부되는지 확인합니다.
    /// </summary>
    [Theory]
    [InlineData(0)]
    [InlineData(7)]
    [InlineData(20)]
    public void DecodeRejectsCiphertextTagAndAssociatedDataTampering(int tamperOffset)
    {
        const ulong sequence = 9;
        using var aes = new AesGcm(Key, CryptoHelper.AuthTagSize);
        var packet = BuildPacket(false, sequence, [1, 2, 3]);
        NetBuffer.EncodePacket(aes, packet, sequence, PacketDirection.ServerToClient, Salt, false);
        var bytes = packet.GetPacketBuffer();
        var actualOffset = tamperOffset == 20 ? bytes.Length - 1 : tamperOffset;
        bytes[actualOffset] ^= 0x80;
        var tampered = FromBytes(bytes);
        var encodedLength = tampered.GetLength();

        Assert.False(NetBuffer.DecodePacket(
            aes, tampered, false, Salt, PacketDirection.ServerToClient, out var failure));
        Assert.Equal(DecodePacketFailureReason.AuthDecryptFailed, failure.Reason);
        Assert.Equal(encodedLength, tampered.GetLength());
    }

    /// <summary>
    /// 패킷 방향, 세션 키 또는 salt가 다르면 복호화가 실패하는지 확인합니다.
    /// </summary>
    [Fact]
    public void DecodeRejectsWrongDirectionKeyAndSalt()
    {
        const ulong sequence = 42;
        using var aes = new AesGcm(Key, CryptoHelper.AuthTagSize);
        var packet = BuildPacket(false, sequence, [1, 2, 3]);
        NetBuffer.EncodePacket(aes, packet, sequence, PacketDirection.ClientToServer, Salt, false);
        var encoded = packet.GetPacketBuffer();

        AssertDecodeFails(encoded, aes, Salt, PacketDirection.ServerToClient);
        var wrongSalt = Salt.ToArray();
        wrongSalt[1] ^= 1;
        AssertDecodeFails(encoded, aes, wrongSalt, PacketDirection.ClientToServer);
        using var wrongAes = new AesGcm(Enumerable.Repeat((byte)0x55, 16).ToArray(), CryptoHelper.AuthTagSize);
        AssertDecodeFails(encoded, wrongAes, Salt, PacketDirection.ClientToServer);
    }

    /// <summary>
    /// header는 있지만 인증 태그가 없는 패킷을 잘못된 레이아웃으로 보고하는지 확인합니다.
    /// </summary>
    [Fact]
    public void DecodeReportsInvalidLayoutWhenHeaderExistsButTagDoesNot()
    {
        var packet = new NetBuffer(32);
        packet.WriteBytes(new byte[14]);
        using var aes = new AesGcm(Key, CryptoHelper.AuthTagSize);

        Assert.False(NetBuffer.DecodePacket(
            aes, packet, true, Salt, PacketDirection.ClientToServer, out var failure));
        Assert.Equal(DecodePacketFailureReason.InvalidLayout, failure.Reason);
        Assert.Equal(14, failure.PacketLength);
        Assert.True(failure.BodySize < 0);
    }

    /// <summary>
    /// nonce가 패킷 방향, salt 및 big-endian 시퀀스를 규약대로 결합하는지 확인합니다.
    /// </summary>
    [Fact]
    public void WriteNonceCombinesDirectionSaltAndBigEndianSequence()
    {
        Span<byte> nonce = stackalloc byte[CryptoHelper.NonceSize];

        CryptoHelper.WriteNonce(nonce, Salt, 0x0102030405060708UL, PacketDirection.ServerToClientReply);

        Assert.Equal((byte)(0xC0 | (Salt[0] & 0x3F)), nonce[0]);
        Assert.Equal(Salt.AsSpan(1, 3).ToArray(), nonce[1..4].ToArray());
        Assert.Equal(new byte[] { 1, 2, 3, 4, 5, 6, 7, 8 }, nonce[4..].ToArray());
    }

    /// <summary>
    /// CryptoHelper가 평문을 정상 왕복시키고 변조된 인증 태그를 거부하는지 확인합니다.
    /// </summary>
    [Fact]
    public void CryptoHelperEncryptDecryptRoundTripsAndRejectsModifiedTag()
    {
        var plaintext = new byte[] { 9, 8, 7, 6 };
        var ciphertext = plaintext.ToArray();
        var nonce = new byte[CryptoHelper.NonceSize];
        CryptoHelper.WriteNonce(nonce, Salt, 1, PacketDirection.ClientToServer);
        var tag = new byte[CryptoHelper.AuthTagSize];
        using var aes = new AesGcm(Key, CryptoHelper.AuthTagSize);

        CryptoHelper.Encrypt(aes, ciphertext, 0, ciphertext.Length, nonce, tag, [1, 2, 3]);
        Assert.NotEqual(plaintext, ciphertext);
        var roundTrip = ciphertext.ToArray();
        Assert.True(CryptoHelper.Decrypt(aes, nonce, roundTrip, [1, 2, 3], tag));
        Assert.Equal(plaintext, roundTrip);

        var modifiedTag = tag.ToArray();
        modifiedTag[0] ^= 1;
        Assert.False(CryptoHelper.Decrypt(
            aes, nonce, ciphertext.ToArray(), [1, 2, 3], modifiedTag));
    }

    private static NetBuffer BuildPacket(bool isCorePacket, ulong sequence, byte[] body)
    {
        var packet = new NetBuffer(128);
        packet.ReserveHeader();
        packet.WriteBytes(body);
        packet.InsertPacketType(isCorePacket ? PacketType.HeartbeatType : PacketType.SendType);
        packet.InsertPacketSequence(sequence);
        if (!isCorePacket)
        {
            packet.InsertPacketId(PacketId.TestPacketReq);
        }
        return packet;
    }

    private static NetBuffer FromBytes(byte[] bytes)
    {
        var packet = new NetBuffer(bytes.Length);
        packet.WriteBytes(bytes);
        return packet;
    }

    private static void AssertDecodeFails(
        byte[] encoded,
        AesGcm aes,
        byte[] salt,
        PacketDirection direction)
    {
        var packet = FromBytes(encoded);
        Assert.False(NetBuffer.DecodePacket(aes, packet, false, salt, direction, out var failure));
        Assert.Equal(DecodePacketFailureReason.AuthDecryptFailed, failure.Reason);
    }
}
