using System.Security.Cryptography;

namespace MultiSocketRUDPBotTester.Buffer
{
    public class NetBuffer
    {
        public static byte HeaderCode { get; set; } = 0xCC;

        private const int HeaderSize = 3;
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

        public void WriteByte(byte value) => _buffer[_writePos++] = value;

        public void WriteUShort(ushort value)
        {
            _buffer[_writePos++] = (byte)(value & 0xFF);
            _buffer[_writePos++] = (byte)((value >> 8) & 0xFF);
        }

        public void WriteUInt(uint value)
        {
            _buffer[_writePos++] = (byte)(value & 0xFF);
            _buffer[_writePos++] = (byte)((value >> 8) & 0xFF);
            _buffer[_writePos++] = (byte)((value >> 16) & 0xFF);
            _buffer[_writePos++] = (byte)((value >> 24) & 0xFF);
        }

        public void WriteULong(ulong value)
        {
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
            var bytes = System.Text.Encoding.UTF8.GetBytes(value);
            WriteUShort((ushort)bytes.Length);
            bytes.CopyTo(_buffer, _writePos);
            _writePos += bytes.Length;
        }

        public void WriteBytes(byte[] bytes)
        {
            bytes.CopyTo(_buffer, _writePos);
            _writePos += bytes.Length;
        }

        public void WriteBytes(byte[] bytes, int offset, int count)
        {
            Array.Copy(bytes, offset, _buffer, _writePos, count);
            _writePos += count;
        }

        public byte ReadByte() => _buffer[_readPos++];

        public ushort ReadUShort()
        {
            var v = (ushort)(_buffer[_readPos] | (_buffer[_readPos + 1] << 8));
            _readPos += 2;
            return v;
        }

        public uint ReadUInt()
        {
            var v = (uint)(_buffer[_readPos]
                | (_buffer[_readPos + 1] << 8)
                | (_buffer[_readPos + 2] << 16)
                | (_buffer[_readPos + 3] << 24));
            _readPos += 4;
            return v;
        }

        public ulong ReadULong()
        {
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
            var s = System.Text.Encoding.UTF8.GetString(_buffer, _readPos, len);
            _readPos += len;
            return s;
        }

        public byte[] ReadBytes(int count)
        {
            var result = new byte[count];
            Array.Copy(_buffer, _readPos, result, 0, count);
            _readPos += count;
            return result;
        }

        public void SkipBytes(int count) => _readPos += count;

        public void InsertPacketType(PacketType type)
        {
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
            _buffer[0] = HeaderCode;
            var payloadSize = (ushort)(_writePos - HeaderSize + extraSize);
            _buffer[1] = (byte)(payloadSize & 0xFF);
            _buffer[2] = (byte)((payloadSize >> 8) & 0xFF);
        }

        public byte[] GetPacketBuffer()
        {
            var result = new byte[_writePos];
            Array.Copy(_buffer, result, _writePos);
            return result;
        }

        public int GetLength() => _writePos;

        private static byte[] GenerateNonce(byte[] sessionSalt, ulong packetSequence, PacketDirection direction)
        {
            var nonce = new byte[12];
            var directionBits = (byte)((byte)direction << 6);
            nonce[0] = (byte)(directionBits | (sessionSalt[0] & 0x3F));
            nonce[1] = sessionSalt[1];
            nonce[2] = sessionSalt[2];
            nonce[3] = sessionSalt[3];

            nonce[4]  = (byte)((packetSequence >> 56) & 0xFF);
            nonce[5]  = (byte)((packetSequence >> 48) & 0xFF);
            nonce[6]  = (byte)((packetSequence >> 40) & 0xFF);
            nonce[7]  = (byte)((packetSequence >> 32) & 0xFF);
            nonce[8]  = (byte)((packetSequence >> 24) & 0xFF);
            nonce[9]  = (byte)((packetSequence >> 16) & 0xFF);
            nonce[10] = (byte)((packetSequence >> 8) & 0xFF);
            nonce[11] = (byte)(packetSequence & 0xFF);

            return nonce;
        }

        public static void EncodePacket(
            AesGcm aesGcm,
            NetBuffer packet,
            ulong packetSequence,
            PacketDirection direction,
            byte[] sessionSalt,
            bool isCorePacket)
        {
            var bodyOffset = isCorePacket ? BodyOffsetCorePacket : BodyOffsetFullPacket;
            var bodySize = packet._writePos - bodyOffset;
            if (bodySize < 0)
            {
                bodySize = 0;
            }

            var nonce = GenerateNonce(sessionSalt, packetSequence, direction);
            var tag = new byte[AuthTagSize];

            packet.SetHeader(AuthTagSize);

            const int aadSize = HeaderSize + PacketTypeSize + PacketSequenceSize;
            aesGcm.Encrypt(
                nonce,
                plaintext: packet._buffer.AsSpan(bodyOffset, bodySize),
                ciphertext: packet._buffer.AsSpan(bodyOffset, bodySize),
                tag: tag,
                associatedData: packet._buffer.AsSpan(0, aadSize));

            tag.CopyTo(packet._buffer, packet._writePos);
            packet._writePos += AuthTagSize;
        }

        public static bool DecodePacket(
            AesGcm aesGcm,
            NetBuffer packet,
            bool isCorePacket,
            byte[] sessionSalt,
            PacketDirection direction)
        {
            ulong packetSequence = 0;
            for (var i = 0; i < PacketSequenceSize; i++)
            {
                packetSequence |= ((ulong)packet._buffer[PacketSequenceOffset + i]) << (i * 8);
            }

            var bodyOffset = isCorePacket ? BodyOffsetCorePacket : BodyOffsetFullPacket;
            var authTagOffset = packet._writePos - AuthTagSize;
            var bodySize = authTagOffset - bodyOffset;

            if (bodySize < 0 || authTagOffset < 0)
            {
                return false;
            }

            var nonce = GenerateNonce(sessionSalt, packetSequence, direction);
            var tag = packet._buffer.AsSpan(authTagOffset, AuthTagSize).ToArray();

            try
            {
                const int aadSize = HeaderSize + PacketTypeSize + PacketSequenceSize;
                aesGcm.Decrypt(
                    nonce,
                    ciphertext: packet._buffer.AsSpan(bodyOffset, bodySize),
                    tag: tag,
                    plaintext: packet._buffer.AsSpan(bodyOffset, bodySize),
                    associatedData: packet._buffer.AsSpan(0, aadSize));

                packet._writePos -= AuthTagSize;
                return true;
            }
            catch (CryptographicException)
            {
                return false;
            }
        }
    }
}
