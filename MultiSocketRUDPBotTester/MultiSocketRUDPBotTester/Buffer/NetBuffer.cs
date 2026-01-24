using System.Security.Cryptography;
using System.Text;
using MultiSocketRUDPBotTester.ClientCore;

namespace MultiSocketRUDPBotTester.Buffer
{
    public class NetBuffer
    {
        private const int BufferSize = 1024;
        private const int HeaderSize = 18;
        private const int PacketTypePos = 5;
        private const int PacketSequencePos = 6;
        private const int PacketIdPos = 14;

        private readonly byte[] buffer = new byte[BufferSize];
        private int readPos = HeaderSize;
        private int writePos = HeaderSize;
        private bool isEncoded;

        private readonly Lock bufferLock = new();

        private void WriteByteUnsafe(byte value)
        {
            if (writePos + sizeof(byte) > BufferSize)
            {
                throw new InvalidOperationException("Buffer overflow");
            }
            buffer[writePos++] = value;
        }

        private void WriteUShortUnsafe(ushort value)
        {
            if (writePos + sizeof(ushort) > BufferSize)
            {
                throw new InvalidOperationException("Buffer overflow");
            }
            buffer[writePos++] = (byte)(value & 0xFF);
            buffer[writePos++] = (byte)((value >> 8) & 0xFF);
        }

        private void WriteUIntUnsafe(uint value)
        {
            if (writePos + sizeof(uint) > BufferSize)
            {
                throw new InvalidOperationException("Buffer overflow");
            }
            for (var i = 0; i < 4; i++)
            {
                buffer[writePos++] = (byte)((value >> (8 * i)) & 0xFF);
            }
        }

        private void WriteULongUnsafe(ulong value)
        {
            if (writePos + sizeof(ulong) > BufferSize)
            {
                throw new InvalidOperationException("Buffer overflow");
            }
            for (var i = 0; i < 8; i++)
            {
                buffer[writePos++] = (byte)((value >> (8 * i)) & 0xFF);
            }
        }

        private void WriteBytesUnsafe(byte[] data)
        {
            if (writePos + data.Length > BufferSize)
            {
                throw new InvalidOperationException("Buffer overflow");
            }
            Array.Copy(data, 0, buffer, writePos, data.Length);
            writePos += data.Length;
        }

        private void WriteBytesUnsafe(byte[] data, int offset, int count)
        {
            if (writePos + count > BufferSize)
            {
                throw new InvalidOperationException("Buffer overflow");
            }
            Array.Copy(data, offset, buffer, writePos, count);
            writePos += count;
        }

        private ushort ReadUShortUnsafe()
        {
            if (readPos + sizeof(ushort) > writePos)
            {
                throw new InvalidOperationException("There is not enough data to read.");
            }
            var value = (ushort)(buffer[readPos] | (buffer[readPos + 1] << 8));
            readPos += 2;
            return value;
        }

        private uint ReadUIntUnsafe()
        {
            if (readPos + sizeof(uint) > writePos)
            {
                throw new InvalidOperationException("There is not enough data to read.");
            }
            var value = (uint)(
                buffer[readPos] |
                (buffer[readPos + 1] << 8) |
                (buffer[readPos + 2] << 16) |
                (buffer[readPos + 3] << 24));
            readPos += 4;
            return value;
        }

        private ulong ReadULongUnsafe()
        {
            if (readPos + sizeof(ulong) > writePos)
            {
                throw new InvalidOperationException("There is not enough data to read.");
            }
            ulong value = 0;
            for (var i = 0; i < 8; i++)
            {
                value |= ((ulong)buffer[readPos + i]) << (8 * i);
            }
            readPos += 8;
            return value;
        }

        private byte[] ReadBytesUnsafe(int length)
        {
            if (readPos + length > writePos)
            {
                throw new InvalidOperationException("There is not enough data to read.");
            }
            var result = new byte[length];
            Array.Copy(buffer, readPos, result, 0, length);
            readPos += length;
            return result;
        }

        public void WriteByte(byte value)
        {
            lock (bufferLock)
            {
                WriteByteUnsafe(value);
            }
        }

        public void WriteSByte(sbyte value)
        {
            lock (bufferLock)
            {
                WriteByteUnsafe((byte)value);
            }
        }

        public void WriteUShort(ushort value)
        {
            lock (bufferLock)
            {
                WriteUShortUnsafe(value);
            }
        }

        public void WriteShort(short value)
        {
            lock (bufferLock)
            {
                WriteUShortUnsafe((ushort)value);
            }
        }

        public void WriteUInt(uint value)
        {
            lock (bufferLock)
            {
                WriteUIntUnsafe(value);
            }
        }

        public void WriteInt(int value)
        {
            lock (bufferLock)
            {
                WriteUIntUnsafe((uint)value);
            }
        }

        public void WriteULong(ulong value)
        {
            lock (bufferLock)
            {
                WriteULongUnsafe(value);
            }
        }

        public void WriteLong(long value)
        {
            lock (bufferLock)
            {
                WriteULongUnsafe((ulong)value);
            }
        }

        public void WriteFloat(float value)
        {
            lock (bufferLock)
            {
                var bytes = BitConverter.GetBytes(value);
                WriteBytesUnsafe(bytes);
            }
        }

        public void WriteDouble(double value)
        {
            lock (bufferLock)
            {
                var bytes = BitConverter.GetBytes(value);
                WriteBytesUnsafe(bytes);
            }
        }

        public void WriteString(string value)
        {
            var bytes = Encoding.UTF8.GetBytes(value);

            lock (bufferLock)
            {
                WriteUIntUnsafe((uint)bytes.Length);
                WriteBytesUnsafe(bytes);
            }
        }

        public void WriteBytes(byte[] data)
        {
            lock (bufferLock)
            {
                WriteBytesUnsafe(data);
            }
        }

        public void WriteBytes(byte[] data, int offset, int count)
        {
            lock (bufferLock)
            {
                WriteBytesUnsafe(data, offset, count);
            }
        }

        public byte ReadByte()
        {
            lock (bufferLock)
            {
                if (readPos + sizeof(byte) > writePos)
                {
                    throw new InvalidOperationException("There is not enough data to read.");
                }
                return buffer[readPos++];
            }
        }

        public sbyte ReadSByte()
        {
            lock (bufferLock)
            {
                if (readPos + sizeof(sbyte) > writePos)
                {
                    throw new InvalidOperationException("There is not enough data to read.");
                }
                return (sbyte)buffer[readPos++];
            }
        }

        public ushort ReadUShort()
        {
            lock (bufferLock)
            {
                return ReadUShortUnsafe();
            }
        }

        public short ReadShort()
        {
            lock (bufferLock)
            {
                return (short)ReadUShortUnsafe();
            }
        }

        public uint ReadUInt()
        {
            lock (bufferLock)
            {
                return ReadUIntUnsafe();
            }
        }

        public int ReadInt()
        {
            lock (bufferLock)
            {
                return (int)ReadUIntUnsafe();
            }
        }

        public ulong ReadULong()
        {
            lock (bufferLock)
            {
                return ReadULongUnsafe();
            }
        }

        public long ReadLong()
        {
            lock (bufferLock)
            {
                return (long)ReadULongUnsafe();
            }
        }

        public float ReadFloat()
        {
            lock (bufferLock)
            {
                var bytes = ReadBytesUnsafe(sizeof(float));
                return BitConverter.ToSingle(bytes, 0);
            }
        }

        public double ReadDouble()
        {
            lock (bufferLock)
            {
                var bytes = ReadBytesUnsafe(sizeof(double));
                return BitConverter.ToDouble(bytes, 0);
            }
        }

        public string ReadString()
        {
            lock (bufferLock)
            {
                var length = ReadUIntUnsafe();
                if (readPos + (int)length > writePos)
                {
                    throw new InvalidOperationException($"Invalid string length: {length}");
                }
                var value = Encoding.UTF8.GetString(buffer, readPos, (int)length);
                readPos += (int)length;
                return value;
            }
        }

        public byte[] ReadBytes(int length)
        {
            lock (bufferLock)
            {
                return ReadBytesUnsafe(length);
            }
        }

        public byte[] GetPayload()
        {
            lock (bufferLock)
            {
                var payloadSize = writePos - HeaderSize;
                var payload = new byte[payloadSize];
                Array.Copy(buffer, HeaderSize, payload, 0, payloadSize);
                return payload;
            }
        }

        public int GetLength()
        {
            lock (bufferLock)
            {
                return writePos;
            }
        }

        public byte[] GetPacketBuffer()
        {
            lock (bufferLock)
            {
                var copy = new byte[writePos];
                Array.Copy(buffer, 0, copy, 0, writePos);
                return copy;
            }
        }

        public void InsertPacketType(PacketType type)
        {
            lock (bufferLock)
            {
                buffer[PacketTypePos] = (byte)type;
            }
        }

        public void InsertPacketSequence(PacketSequence seq)
        {
            lock (bufferLock)
            {
                var span = buffer.AsSpan(PacketSequencePos, 8);
                BitConverter.TryWriteBytes(span, seq);
            }
        }

        public void InsertPacketId(PacketId id)
        {
            lock (bufferLock)
            {
                var span = buffer.AsSpan(PacketIdPos, 4);
                BitConverter.TryWriteBytes(span, (uint)id);
            }
        }

        public void BuildConnectPacket(SessionIdType sessionId)
        {
            lock (bufferLock)
            {
                writePos = PacketIdPos;
                WriteByteUnsafe((byte)PacketType.CONNECT_TYPE);
                WriteULongUnsafe(0);
                WriteUShortUnsafe(sessionId);
            }
        }

        public static void EncodePacket(AesGcm aesGcm, NetBuffer buffer, PacketSequence packetSequence, PacketDirection direction, string sessionSalt, bool isCorePacket)
        {
            lock (buffer.bufferLock)
            {
                if (buffer.isEncoded)
                {
                    return;
                }

                var nonce = CryptoHelper.GenerateNonce(Encoding.UTF8.GetBytes(sessionSalt), packetSequence, direction);
                var bodySize = buffer.writePos - (isCorePacket ? (HeaderSize + sizeof(PacketType) + sizeof(PacketSequence)) : (HeaderSize + sizeof(PacketType) + sizeof(PacketSequence) + sizeof(PacketId)));
                var bodyOffset = isCorePacket
                    ? (HeaderSize + sizeof(PacketType) + sizeof(PacketSequence))
                    : (HeaderSize + sizeof(PacketType) + sizeof(PacketSequence) + sizeof(PacketId));
                Span<byte> authTag = stackalloc byte[CryptoHelper.AuthTagSize];

                CryptoHelper.Encrypt(
                    aesGcm,
                    buffer.buffer,
                    bodyOffset,
                    bodySize,
                    nonce,
                    authTag
                );

                buffer.WriteBytesUnsafe(authTag.ToArray());
                buffer.isEncoded = true;
            }
        }

        public static bool DecodePacket(AesGcm aesGcm, NetBuffer packet, bool isCorePacket, byte[] sessionKey, string sessionSalt, PacketDirection direction)
        {
            lock (packet.bufferLock)
            {
                const int minimumPacketSize = sizeof(PacketSequence) + sizeof(PacketId) + CryptoHelper.AuthTagSize;
                const int minimumCorePacketSize = sizeof(PacketSequence) + CryptoHelper.AuthTagSize;
                const int sizeOfHeaderWithPacketType = HeaderSize + sizeof(PacketType);

                var packetUseSize = packet.writePos;
                var minimumRecvPacketSize = isCorePacket ? minimumCorePacketSize : minimumPacketSize;
                if (packetUseSize < minimumRecvPacketSize)
                {
                    return false;
                }

                var packetSequence = packet.ReadULongUnsafe();
                var bodyOffset = isCorePacket
                    ? (sizeOfHeaderWithPacketType + sizeof(PacketSequence))
                    : (sizeOfHeaderWithPacketType + sizeof(PacketSequence) + sizeof(PacketId));
                var authTagOffset = packet.writePos - CryptoHelper.AuthTagSize;
                var bodySize = packetUseSize + sizeOfHeaderWithPacketType - bodyOffset - CryptoHelper.AuthTagSize;

                var nonce = CryptoHelper.GenerateNonce(Encoding.UTF8.GetBytes(sessionSalt), packetSequence, direction);
                if (nonce.Length == 0)
                {
                    return false;
                }

                var bodySpan = packet.buffer.AsSpan(bodyOffset, bodySize);
                var aadSpan = packet.buffer.AsSpan(0, sizeOfHeaderWithPacketType);
                var authTagSpan = packet.buffer.AsSpan(authTagOffset, CryptoHelper.AuthTagSize);

                return CryptoHelper.Decrypt(
                    aesGcm,
                    sessionKey,
                    nonce,
                    bodySpan,
                    aadSpan,
                    authTagSpan);
            }
        }
    }
}