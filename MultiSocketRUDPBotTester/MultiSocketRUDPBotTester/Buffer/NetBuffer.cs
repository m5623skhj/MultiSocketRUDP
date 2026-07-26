using MultiSocketRUDPBotTester.ClientCore;
using System.Security.Cryptography;

namespace MultiSocketRUDPBotTester.Buffer
{
    public enum DecodePacketFailureReason
    {
        None,
        InvalidLayout,
        AuthDecryptFailed
    }

    public readonly record struct DecodePacketFailureDetails(
        DecodePacketFailureReason Reason,
        ulong PacketSequence,
        PacketDirection Direction,
        bool IsCorePacket,
        int PacketLength,
        int HeaderPayloadSize,
        int BodySize,
        int AuthTagOffset);

    public class NetBuffer
    {
        public static byte HeaderCode { get; set; } = 0xCC;

        private const int HeaderSize = 5;
        private const int PacketTypeSize = 1;
        private const int PacketSequenceSize = 8;
        private const int PacketIdSize = 4;
        private const int AuthTagSize = 16;

        private const int PacketSequenceOffset = HeaderSize + PacketTypeSize;
        private const int BodyOffsetCorePacket = HeaderSize + PacketTypeSize + PacketSequenceSize;
        private const int BodyOffsetFullPacket = BodyOffsetCorePacket + PacketIdSize;

        private readonly byte[] _buffer;
        private int _readPos;
        private int _writePos;

        public NetBuffer(int capacity = 65536)
        {
            _buffer = new byte[capacity];
            _readPos = 0;
            _writePos = 0;
        }

        public void ReserveHeader()
        {
            EnsureWritableFrom(0, HeaderSize);
            _readPos = 0;
            _writePos = HeaderSize;
        }

        public void WriteByte(byte value)
        {
            EnsureWritable(1);
            _buffer[_writePos++] = value;
        }

        public void WriteUShort(ushort value)
        {
            EnsureWritable(sizeof(ushort));
            _buffer[_writePos++] = (byte)(value & 0xFF);
            _buffer[_writePos++] = (byte)((value >> 8) & 0xFF);
        }

        public void WriteUInt(uint value)
        {
            EnsureWritable(sizeof(uint));
            _buffer[_writePos++] = (byte)(value & 0xFF);
            _buffer[_writePos++] = (byte)((value >> 8) & 0xFF);
            _buffer[_writePos++] = (byte)((value >> 16) & 0xFF);
            _buffer[_writePos++] = (byte)((value >> 24) & 0xFF);
        }

        public void WriteInt(int value)
        {
            EnsureWritable(sizeof(int));
            _buffer[_writePos++] = (byte)(value & 0xFF);
            _buffer[_writePos++] = (byte)((value >> 8) & 0xFF);
            _buffer[_writePos++] = (byte)((value >> 16) & 0xFF);
            _buffer[_writePos++] = (byte)((value >> 24) & 0xFF);
        }

        public void WriteULong(ulong value)
        {
            EnsureWritable(sizeof(ulong));
            _buffer[_writePos++] = (byte)(value & 0xFF);
            _buffer[_writePos++] = (byte)((value >> 8) & 0xFF);
            _buffer[_writePos++] = (byte)((value >> 16) & 0xFF);
            _buffer[_writePos++] = (byte)((value >> 24) & 0xFF);
            _buffer[_writePos++] = (byte)((value >> 32) & 0xFF);
            _buffer[_writePos++] = (byte)((value >> 40) & 0xFF);
            _buffer[_writePos++] = (byte)((value >> 48) & 0xFF);
            _buffer[_writePos++] = (byte)((value >> 56) & 0xFF);
        }

        public void WriteString(string value)
        {
            ArgumentNullException.ThrowIfNull(value);
            var bytes = System.Text.Encoding.UTF8.GetBytes(value);
            if (bytes.Length > ushort.MaxValue)
            {
                throw new ArgumentOutOfRangeException(
                    nameof(value),
                    $"UTF-8 string length cannot exceed {ushort.MaxValue} bytes.");
            }

            EnsureWritable(sizeof(ushort) + bytes.Length);
            WriteUShort((ushort)bytes.Length);
            bytes.CopyTo(_buffer, _writePos);
            _writePos += bytes.Length;
        }

        public void WriteBytes(byte[] bytes)
        {
            ArgumentNullException.ThrowIfNull(bytes);
            EnsureWritable(bytes.Length);
            bytes.CopyTo(_buffer, _writePos);
            _writePos += bytes.Length;
        }

        public void WriteBytes(byte[] bytes, int offset, int count)
        {
            ArgumentNullException.ThrowIfNull(bytes);
            ArgumentOutOfRangeException.ThrowIfNegative(offset);
            ArgumentOutOfRangeException.ThrowIfNegative(count);
            if (offset > bytes.Length - count)
            {
                throw new ArgumentException("The requested source range exceeds the byte array.");
            }

            EnsureWritable(count);
            Array.Copy(bytes, offset, _buffer, _writePos, count);
            _writePos += count;
        }

        public byte ReadByte()
        {
            EnsureReadable(1);
            return _buffer[_readPos++];
        }

        public ushort ReadUShort()
        {
            EnsureReadable(sizeof(ushort));
            var v = (ushort)(_buffer[_readPos] | (_buffer[_readPos + 1] << 8));
            _readPos += 2;
            return v;
        }

        public uint ReadUInt()
        {
            EnsureReadable(sizeof(uint));
            var v = (uint)(_buffer[_readPos]
                | (_buffer[_readPos + 1] << 8)
                | (_buffer[_readPos + 2] << 16)
                | (_buffer[_readPos + 3] << 24));
            _readPos += 4;
            return v;
        }

        public int ReadInt()
        {
            EnsureReadable(sizeof(int));
            var v = (int)(_buffer[_readPos]
                | (_buffer[_readPos + 1] << 8)
                | (_buffer[_readPos + 2] << 16)
                | (_buffer[_readPos + 3] << 24));
            _readPos += 4;
            return v;
        }

        public ulong ReadULong()
        {
            EnsureReadable(sizeof(ulong));
            var v = (ulong)_buffer[_readPos]
                | ((ulong)_buffer[_readPos + 1] << 8)
                | ((ulong)_buffer[_readPos + 2] << 16)
                | ((ulong)_buffer[_readPos + 3] << 24)
                | ((ulong)_buffer[_readPos + 4] << 32)
                | ((ulong)_buffer[_readPos + 5] << 40)
                | ((ulong)_buffer[_readPos + 6] << 48)
                | ((ulong)_buffer[_readPos + 7] << 56);
            _readPos += 8;
            return v;
        }

        public string ReadString()
        {
            var len = ReadUShort();
            EnsureReadable(len);
            var s = System.Text.Encoding.UTF8.GetString(_buffer, _readPos, len);
            _readPos += len;
            return s;
        }

        public byte[] ReadBytes(int count)
        {
            ArgumentOutOfRangeException.ThrowIfNegative(count);
            EnsureReadable(count);
            var result = new byte[count];
            Array.Copy(_buffer, _readPos, result, 0, count);
            _readPos += count;
            return result;
        }

        public void SkipBytes(int count)
        {
            ArgumentOutOfRangeException.ThrowIfNegative(count);
            EnsureReadable(count);
            _readPos += count;
        }

        public void InsertPacketType(PacketType type)
        {
            EnsureMetadataLayout(HeaderSize);
            EnsureWritable(PacketTypeSize);
            var bodyLen = _writePos - HeaderSize;
            if (bodyLen > 0)
            {
                Array.Copy(_buffer, HeaderSize, _buffer, HeaderSize + PacketTypeSize, bodyLen);
            }

            _buffer[HeaderSize] = (byte)type;
            _writePos += PacketTypeSize;
        }

        public void InsertPacketSequence(ulong sequence)
        {
            var afterType = HeaderSize + PacketTypeSize;
            EnsureMetadataLayout(afterType);
            EnsureWritable(PacketSequenceSize);
            var bodyLen = _writePos - afterType;
            if (bodyLen > 0)
            {
                Array.Copy(_buffer, afterType, _buffer, afterType + PacketSequenceSize, bodyLen);
            }

            for (var i = 0; i < PacketSequenceSize; i++)
            {
                _buffer[afterType + i] = (byte)((sequence >> (i * 8)) & 0xFF);
            }

            _writePos += PacketSequenceSize;
        }

        public void InsertPacketId(PacketId packetId)
        {
            var afterSeq = HeaderSize + PacketTypeSize + PacketSequenceSize;
            EnsureMetadataLayout(afterSeq);
            EnsureWritable(PacketIdSize);
            var bodyLen = _writePos - afterSeq;
            if (bodyLen > 0)
            {
                Array.Copy(_buffer, afterSeq, _buffer, afterSeq + PacketIdSize, bodyLen);
            }

            var id = (uint)packetId;
            for (var i = 0; i < PacketIdSize; i++)
            {
                _buffer[afterSeq + i] = (byte)((id >> (i * 8)) & 0xFF);
            }

            _writePos += PacketIdSize;
        }

        public void BuildConnectPacket(SessionIdType sessionId)
        {
            _readPos = HeaderSize;
            _writePos = HeaderSize;

            WriteByte((byte)PacketType.ConnectType);
            WriteULong(0);
            WriteUShort(sessionId);
        }

        public void BuildCorePacket(PacketType packetType, ulong sequence)
        {
            _readPos = HeaderSize;
            _writePos = HeaderSize;

            WriteByte((byte)packetType);
            WriteULong(sequence);
        }

        private void SetHeader(int extraSize = 0)
        {
            EnsureMetadataLayout(HeaderSize);
            if (extraSize < 0)
            {
                throw new ArgumentOutOfRangeException(nameof(extraSize));
            }

            var payloadLength = _writePos - HeaderSize + extraSize;
            if (payloadLength > ushort.MaxValue)
            {
                throw new InvalidOperationException(
                    $"Packet payload length cannot exceed {ushort.MaxValue} bytes.");
            }

            _buffer[0] = HeaderCode;
            var payloadSize = (ushort)payloadLength;
            _buffer[1] = (byte)(payloadSize & 0xFF);
            _buffer[2] = (byte)((payloadSize >> 8) & 0xFF);
        }

        public ReadOnlyMemory<byte> GetPacketMemory()
        {
            return new ReadOnlyMemory<byte>(_buffer, 0, _writePos);
        }

        public byte[] GetPacketBuffer()
        {
            var result = new byte[_writePos];
            Array.Copy(_buffer, result, _writePos);
            return result;
        }

        public int GetLength() => _writePos;

        public static void EncodePacket(
            AesGcm aesGcm,
            NetBuffer packet,
            ulong packetSequence,
            PacketDirection direction,
            byte[] sessionSalt,
            bool isCorePacket)
        {
            var bodyOffset = isCorePacket ? BodyOffsetCorePacket : BodyOffsetFullPacket;
            if (packet._writePos < bodyOffset)
            {
                throw new InvalidOperationException(
                    $"Packet layout is shorter than the required body offset {bodyOffset}.");
            }

            packet.EnsureWritable(AuthTagSize);
            var bodySize = packet._writePos - bodyOffset;
            Span<byte> nonce = stackalloc byte[CryptoHelper.NonceSize];
            CryptoHelper.WriteNonce(nonce, sessionSalt, packetSequence, direction);

            packet.SetHeader(AuthTagSize);

            const int aadSize = HeaderSize + PacketTypeSize + PacketSequenceSize;
            var tagDest = packet._buffer.AsSpan(packet._writePos, AuthTagSize);
            aesGcm.Encrypt(
                nonce,
                plaintext: packet._buffer.AsSpan(bodyOffset, bodySize),
                ciphertext: packet._buffer.AsSpan(bodyOffset, bodySize),
                tag: tagDest,
                associatedData: packet._buffer.AsSpan(0, aadSize));

            packet._writePos += AuthTagSize;
        }

        public static bool DecodePacket(
            AesGcm aesGcm,
            NetBuffer packet,
            bool isCorePacket,
            byte[] sessionSalt,
            PacketDirection direction)
        {
            return DecodePacket(
                aesGcm,
                packet,
                isCorePacket,
                sessionSalt,
                direction,
                out _);
        }

        public static bool DecodePacket(
            AesGcm aesGcm,
            NetBuffer packet,
            bool isCorePacket,
            byte[] sessionSalt,
            PacketDirection direction,
            out DecodePacketFailureDetails outFailureDetails)
        {
            var bodyOffset = isCorePacket ? BodyOffsetCorePacket : BodyOffsetFullPacket;
            var authTagOffset = packet._writePos - AuthTagSize;
            var bodySize = authTagOffset - bodyOffset;
            var hasHeader = packet._writePos >= HeaderSize;
            var headerPayloadSize = hasHeader
                ? packet._buffer[1] | (packet._buffer[2] << 8)
                : 0;

            if (packet._writePos < PacketSequenceOffset + PacketSequenceSize
                || bodySize < 0
                || authTagOffset < bodyOffset
                || headerPayloadSize != packet._writePos - HeaderSize)
            {
                outFailureDetails = new DecodePacketFailureDetails(
                    DecodePacketFailureReason.InvalidLayout,
                    0,
                    direction,
                    isCorePacket,
                    packet._writePos,
                    headerPayloadSize,
                    bodySize,
                    authTagOffset);
                return false;
            }

            ulong packetSequence = 0;
            for (var i = 0; i < PacketSequenceSize; i++)
            {
                packetSequence |= ((ulong)packet._buffer[PacketSequenceOffset + i]) << (i * 8);
            }

            Span<byte> nonce = stackalloc byte[CryptoHelper.NonceSize];
            CryptoHelper.WriteNonce(nonce, sessionSalt, packetSequence, direction);

            try
            {
                const int aadSize = HeaderSize + PacketTypeSize + PacketSequenceSize;
                aesGcm.Decrypt(
                    nonce,
                    ciphertext: packet._buffer.AsSpan(bodyOffset, bodySize),
                    tag: packet._buffer.AsSpan(authTagOffset, AuthTagSize),
                    plaintext: packet._buffer.AsSpan(bodyOffset, bodySize),
                    associatedData: packet._buffer.AsSpan(0, aadSize));

                packet._writePos -= AuthTagSize;
                outFailureDetails = default;
                return true;
            }
            catch (CryptographicException)
            {
                outFailureDetails = new DecodePacketFailureDetails(
                    DecodePacketFailureReason.AuthDecryptFailed,
                    packetSequence,
                    direction,
                    isCorePacket,
                    packet._writePos,
                    headerPayloadSize,
                    bodySize,
                    authTagOffset);
                return false;
            }
        }

        private void EnsureReadable(int count)
        {
            if (count < 0 || _readPos > _writePos - count)
            {
                throw new InvalidOperationException(
                    $"Buffer underflow: tried to read {count} bytes at position {_readPos}, written size {_writePos}.");
            }
        }

        private void EnsureWritable(int count)
        {
            EnsureWritableFrom(_writePos, count);
        }

        private void EnsureWritableFrom(int offset, int count)
        {
            if (count < 0 || offset < 0 || offset > _buffer.Length - count)
            {
                throw new InvalidOperationException(
                    $"Buffer overflow: tried to write {count} bytes at position {offset}, capacity {_buffer.Length}.");
            }
        }

        private void EnsureMetadataLayout(int minimumLength)
        {
            if (_writePos < minimumLength)
            {
                throw new InvalidOperationException(
                    $"Packet metadata insertion requires at least {minimumLength} written bytes.");
            }
        }
    }
}
