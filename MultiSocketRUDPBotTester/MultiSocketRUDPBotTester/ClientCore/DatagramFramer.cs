namespace MultiSocketRUDPBotTester.ClientCore
{
    internal static class DatagramFramer
    {
        private const int HeaderSize = 5;

        public static bool TryGetPacketSize(
            ReadOnlySpan<byte> data,
            int offset,
            out int packetSize)
        {
            packetSize = 0;
            if (offset < 0 || offset > data.Length - HeaderSize)
            {
                return false;
            }

            var payloadLength = data[offset + 1] | (data[offset + 2] << 8);
            if (payloadLength <= 0)
            {
                return false;
            }

            packetSize = HeaderSize + payloadLength;
            return true;
        }
    }
}
