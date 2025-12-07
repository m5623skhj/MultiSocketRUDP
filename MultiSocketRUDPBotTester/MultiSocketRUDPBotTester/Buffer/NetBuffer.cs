using System.Text;

namespace MultiSocketRUDPBotTester.Buffer
{
    public class NetBuffer
    {
        private const int BUFFER_SIZE = 1024;
        private const int HEADER_SIZE = 18;
        private const int PACKET_TYPE_POS = 5;
        private const int PACKET_SEQUENCE_POS = 6;
        private const int PACKET_ID_POS = 14;
        private byte[] buffer;
        private int readPos;
        private int writePos;
        private bool isEndoded = false;

        public static byte HeaderCode { get; set; } = 0x89;
        public static byte XORKey { get; set; } = 0x32;

        public NetBuffer()
        {
            buffer = new byte[BUFFER_SIZE];
            readPos = HEADER_SIZE;
            writePos = HEADER_SIZE;
        }

        public void WriteByte(byte value)
        {
            buffer[writePos++] = value;
        }

        public void WriteSByte(sbyte value)
        {
            buffer[writePos++] = (byte)value;
        }

        public void WriteUShort(ushort value)
        {
            buffer[writePos++] = (byte)(value & 0xFF);
            buffer[writePos++] = (byte)((value >> 8) & 0xFF);
        }

        public void WriteShort(short value)
        {
            WriteUShort((ushort)value);
        }

        public void WriteUInt(uint value)
        {
            for (var i = 0; i < 4; i++)
            {
                buffer[writePos++] = (byte)((value >> (8 * i)) & 0xFF);
            }
        }

        public void WriteInt(int value)
        {
            WriteUInt((uint)value);
        }

        public void WriteULong(ulong value)
        {
            for (var i = 0; i < 8; i++)
                buffer[writePos++] = (byte)((value >> (8 * i)) & 0xFF);
        }

        public void WriteLong(long value)
        {
            WriteULong((ulong)value);
        }

        public void WriteFloat(float value)
        {
            WriteBytes(BitConverter.GetBytes(value));
        }

        public void WriteDouble(double value)
        {
            WriteBytes(BitConverter.GetBytes(value));
        }

        public void WriteString(string value)
        {
            var bytes = Encoding.UTF8.GetBytes(value);
            WriteUInt((uint)bytes.Length);
            WriteBytes(bytes);
        }

        public void WriteBytes(byte[] data)
        {
            Array.Copy(data, 0, buffer, writePos, data.Length);
            writePos += data.Length;
        }

        public void WriteBytes(byte[] data, int offset, int count)
        {
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
                value |= ((ulong)buffer[readPos + i]) << (8 * i);
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

        public void Encode()
        {
            if (isEndoded)
            {
                return;
            }

            var payloadLength = writePos - HEADER_SIZE;

            buffer[0] = HeaderCode;
            buffer[1] = (byte)(payloadLength & 0xFF);
            buffer[2] = (byte)((payloadLength >> 8) & 0xFF);

            var randCode = (byte)(new Random().Next(0, 256));
            buffer[3] = randCode;

            var payloadSum = 0;
            for (var i = HEADER_SIZE; i < writePos; i++)
            {
                payloadSum += buffer[i];
            }

            buffer[4] = (byte)(payloadSum & 0xFF);

            byte beforeRandomXOR = 0;
            byte beforeFixedXOR = 0;
            var numOfRoutine = 1;

            for (var i = 4; i < writePos; i++)
            {
                var cur = buffer[i];
                beforeRandomXOR = (byte)(cur ^ (beforeRandomXOR + randCode + numOfRoutine));
                beforeFixedXOR = (byte)(beforeRandomXOR ^ (beforeFixedXOR + XORKey + numOfRoutine));
                buffer[i] = beforeFixedXOR;
                numOfRoutine++;
            }

            var result = new byte[writePos];
            Array.Copy(buffer, result, writePos);
        }

        public bool Decode(byte[] data)
        {
            if (data.Length < HEADER_SIZE) return false;

            var randCode = data[3];
            var xorCode = XORKey;
            var numOfRoutine = 1;

            var saveEncoded = data[4];
            var saveRandom = (byte)(data[4] ^ (xorCode + numOfRoutine));
            data[4] = (byte)(saveRandom ^ (randCode + numOfRoutine));

            numOfRoutine++;
            var beforeEncoded = saveEncoded;
            var beforeRandom = saveRandom;

            var payloadSum = 0;

            for (var i = HEADER_SIZE; i < data.Length; i++)
            {
                saveEncoded = data[i];
                saveRandom = (byte)(data[i] ^ (beforeEncoded + xorCode + numOfRoutine));
                data[i] = (byte)(saveRandom ^ (beforeRandom + randCode + numOfRoutine));
                numOfRoutine++;

                payloadSum += data[i];
                beforeEncoded = saveEncoded;
                beforeRandom = saveRandom;
            }

            if (data[4] != (byte)(payloadSum & 0xFF))
            {
                return false;
            }

            Array.Copy(data, buffer, data.Length);
            readPos = HEADER_SIZE;
            writePos = data.Length;
            return true;
        }

        public byte[] GetPayload()
        {
            var payloadSize = writePos - HEADER_SIZE;
            var payload = new byte[payloadSize];
            Array.Copy(buffer, HEADER_SIZE, payload, 0, payloadSize);
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
            buffer[PACKET_TYPE_POS] = (byte)type;
        }

        public void InsertPacketSequence(PacketSequence seq)
        {
            var span = buffer.AsSpan(PACKET_SEQUENCE_POS, 8);
            BitConverter.TryWriteBytes(span, seq);
        }

        public void InsertPacketId(PacketId id)
        {
            var span = buffer.AsSpan(PACKET_ID_POS, 4);
            BitConverter.TryWriteBytes(span, id);
        }

        public void BuildConnectPacket(ushort sessionId, string sessionKey)
        {
            writePos = PACKET_ID_POS;
            WriteUShort(sessionId);
            WriteString(sessionKey);
        }
    }
}
