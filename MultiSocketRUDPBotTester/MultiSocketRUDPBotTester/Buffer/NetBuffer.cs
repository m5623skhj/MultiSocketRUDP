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

        public void WriteByte(byte value)
        {
            if (writePos + sizeof(byte) > BufferSize)
            {
                throw new InvalidOperationException("Buffer overflow");
            }

            buffer[writePos++] = value;
        }

        public void WriteSByte(sbyte value)
        {
            if (writePos + sizeof(sbyte) > BufferSize)
            {
                throw new InvalidOperationException("Buffer overflow");
            }

            buffer[writePos++] = (byte)value;
        }

        public void WriteUShort(ushort value)
        {
            if (writePos + sizeof(ushort) > BufferSize)
            {
                throw new InvalidOperationException("Buffer overflow");
            }

            buffer[writePos++] = (byte)(value & 0xFF);
            buffer[writePos++] = (byte)((value >> 8) & 0xFF);
        }

        public void WriteShort(short value)
        {
            if (writePos + sizeof(short) > BufferSize)
            {
                throw new InvalidOperationException("Buffer overflow");
            }

            WriteUShort((ushort)value);
        }

        public void WriteUInt(uint value)
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

        public void WriteInt(int value)
        {
            if (writePos + sizeof(int) > BufferSize)
            {
                throw new InvalidOperationException("Buffer overflow");
            }

            WriteUInt((uint)value);
        }

        public void WriteULong(ulong value)
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

        public void WriteLong(long value)
        {
            if (writePos + sizeof(long) > BufferSize)
            {
                throw new InvalidOperationException("Buffer overflow");
            }

            WriteULong((ulong)value);
        }

        public void WriteFloat(float value)
        {
            if (writePos + sizeof(float) > BufferSize)
            {
                throw new InvalidOperationException("Buffer overflow");
            }

            WriteBytes(BitConverter.GetBytes(value));
        }

        public void WriteDouble(double value)
        {
            if (writePos + sizeof(double) > BufferSize)
            {
                throw new InvalidOperationException("Buffer overflow");
            }

            WriteBytes(BitConverter.GetBytes(value));
        }

        public void WriteString(string value)
        {
            var bytes = Encoding.UTF8.GetBytes(value);
            if (writePos + sizeof(uint) + bytes.Length > BufferSize)
            {
                throw new InvalidOperationException("Buffer overflow");
            }

            WriteUInt((uint)bytes.Length);
            WriteBytes(bytes);
        }

        public void WriteBytes(byte[] data)
        {
            if (writePos + data.Length > BufferSize)
            {
                throw new InvalidOperationException("Buffer overflow");
            }

            Array.Copy(data, 0, buffer, writePos, data.Length);
            writePos += data.Length;
        }

        public void WriteBytes(byte[] data, int offset, int count)
        {
            if (writePos + count > BufferSize)
            {
                throw new InvalidOperationException("Buffer overflow");
            }

            Array.Copy(data, offset, buffer, writePos, count);
            writePos += count;
        }

        public byte ReadByte()
        {
            return buffer[readPos++];
        }

        public sbyte ReadSByte()
        {
            return (sbyte)buffer[readPos++];
        }

        public ushort ReadUShort()
        {
            var value = (ushort)(buffer[readPos] | (buffer[readPos + 1] << 8));
            readPos += 2;

            return value;
        }

        public short ReadShort()
        {
            return (short)ReadUShort();
        }

        public uint ReadUInt()
        {
            var value = (uint)(
                buffer[readPos] |
                (buffer[readPos + 1] << 8) |
                (buffer[readPos + 2] << 16) |
                (buffer[readPos + 3] << 24));
            readPos += 4;

            return value;
        }

        public int ReadInt()
        {
            return (int)ReadUInt();
        }

        public ulong ReadULong()
        {
            ulong value = 0;
            for (var i = 0; i < 8; i++)
            {
                value |= ((ulong)buffer[readPos + i]) << (8 * i);
            }

            readPos += 8;

            return value;
        }

        public long ReadLong()
        {
            return (long)ReadULong();
        }

        public float ReadFloat()
        {
            var bytes = ReadBytes(sizeof(float));
            return BitConverter.ToSingle(bytes, 0);
        }

        public double ReadDouble()
        {
            var bytes = ReadBytes(sizeof(double));
            return BitConverter.ToDouble(bytes, 0);
        }

        public string ReadString()
        {
            var length = ReadUInt();
            var value = Encoding.UTF8.GetString(buffer, readPos, (int)length);
            readPos += (int)length;

            return value;
        }

        public byte[] ReadBytes(int length)
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

        public static void EncodePacket(AesGcm aesGcm, NetBuffer buffer, PacketSequence packetSequence, PacketDirection direction, string sessionSalt, bool isCorePacket)
        {
            if (buffer.isEncoded)
            {
                return;
            }

            var nonce = CryptoHelper.GenerateNonce(Encoding.UTF8.GetBytes(sessionSalt), packetSequence, direction);
            var bodySize = buffer.GetLength() - (isCorePacket ? (HeaderSize + sizeof(PacketType) + sizeof(PacketSequence)) : (HeaderSize + sizeof(PacketType) + sizeof(PacketSequence) + sizeof(PacketId)));
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

            buffer.WriteBytes(authTag.ToArray());
            buffer.isEncoded = true;
        }

        public static bool DecodePacket(AesGcm aesGcm, NetBuffer packet, bool isCorePacket, byte[] sessionKey, string sessionSalt, PacketDirection direction)
        {
            const int minimumPacketSize = sizeof(PacketSequence) + sizeof(PacketId) + CryptoHelper.AuthTagSize;
            const int minimumCorePacketSize = sizeof(PacketSequence) + CryptoHelper.AuthTagSize;
            const int sizeOfHeaderWithPacketType = HeaderSize + sizeof(PacketType);

            var packetUseSize = packet.GetLength();
            var minimumRecvPacketSize = isCorePacket ? minimumCorePacketSize : minimumPacketSize;
            if (packetUseSize < minimumRecvPacketSize)
            {
                return false;
            }

            var packetSequence = packet.ReadULong();
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

        public byte[] GetPayload()
        {
            var payloadSize = writePos - HeaderSize;
            var payload = new byte[payloadSize];
            Array.Copy(buffer, HeaderSize, payload, 0, payloadSize);

            return payload;
        }

        public int GetLength()
        {
            return writePos;
        }

        public byte[] GetPacketBuffer()
        {
            return buffer;
        }

        public void InsertPacketType(PacketType type)
        {
            buffer[PacketTypePos] = (byte)type;
        }

        public void InsertPacketSequence(PacketSequence seq)
        {
            var span = buffer.AsSpan(PacketSequencePos, 8);
            BitConverter.TryWriteBytes(span, seq);
        }

        public void InsertPacketId(PacketId id)
        {
            var span = buffer.AsSpan(PacketIdPos, 4);
            BitConverter.TryWriteBytes(span, (uint)id);
        }

        public void BuildConnectPacket(SessionIdType sessionId)
        {
            writePos = PacketIdPos;
            WriteByte((byte)PacketType.CONNECT_TYPE);
            WriteULong(0);
            WriteUShort(sessionId);
        }
    }
}
