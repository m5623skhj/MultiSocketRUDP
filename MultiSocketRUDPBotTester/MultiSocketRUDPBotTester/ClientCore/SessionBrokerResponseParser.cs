using System.Buffers.Binary;
using System.IO;
using System.Text;

namespace MultiSocketRUDPBotTester.ClientCore
{
    internal readonly record struct ParsedSessionBrokerResponse(
        string ServerIp,
        ushort ServerPort,
        SessionIdType SessionId,
        byte[] SessionKey,
        byte[] SessionSalt);

    internal static class SessionBrokerResponseParser
    {
        public static ParsedSessionBrokerResponse Parse(ReadOnlySpan<byte> data)
        {
            var offset = 0;
            var resultCode = (ConnectResultCode)ReadByte(data, ref offset);
            if (resultCode != ConnectResultCode.Success)
            {
                throw new InvalidOperationException($"Session broker response error: {resultCode}");
            }

            var serverIp = ReadString(data, ref offset);
            var serverPort = ReadUShort(data, ref offset);
            var sessionId = ReadUShort(data, ref offset);
            var sessionKey = ReadBytes(data, ref offset, SessionInfo.SessionKeySize);
            var sessionSalt = ReadBytes(data, ref offset, SessionInfo.SessionSaltSize);

            return new ParsedSessionBrokerResponse(
                serverIp,
                serverPort,
                sessionId,
                sessionKey,
                sessionSalt);
        }

        private static byte ReadByte(ReadOnlySpan<byte> data, ref int offset)
        {
            EnsureAvailable(data, offset, sizeof(byte));
            return data[offset++];
        }

        private static ushort ReadUShort(ReadOnlySpan<byte> data, ref int offset)
        {
            EnsureAvailable(data, offset, sizeof(ushort));
            var value = BinaryPrimitives.ReadUInt16LittleEndian(data[offset..]);
            offset += sizeof(ushort);
            return value;
        }

        private static string ReadString(ReadOnlySpan<byte> data, ref int offset)
        {
            var length = ReadUShort(data, ref offset);
            EnsureAvailable(data, offset, length);
            var value = Encoding.UTF8.GetString(data.Slice(offset, length));
            offset += length;
            return value;
        }

        private static byte[] ReadBytes(ReadOnlySpan<byte> data, ref int offset, int count)
        {
            EnsureAvailable(data, offset, count);
            var value = data.Slice(offset, count).ToArray();
            offset += count;
            return value;
        }

        private static void EnsureAvailable(ReadOnlySpan<byte> data, int offset, int count)
        {
            if (offset < 0 || count < 0 || offset > data.Length - count)
            {
                throw new InvalidDataException(
                    $"Session broker response is truncated at offset {offset}; required bytes: {count}.");
            }
        }
    }
}
