using MultiSocketRUDPBotTester.Buffer;

namespace ClientCore
{
    public class SendPacketInfo
    {
        public SendPacketInfo(NetBuffer inSentBuffer)
        {
            SentBuffer = inSentBuffer;
        }

        public void RefreshSendPacketInfo(UInt64 now)
        {
            sendTimeStamp = now;
            ++retransmissionCount;
        }

        public bool IsRetransmissionTime(UInt64 now)
        {
            return (now - sendTimeStamp) >= RETRANSMISSION_TIMEOUT_MS;
        }

        public bool IsExceedMaxRetransmissionCount()
        {
            return retransmissionCount >= RETRANSMISSION_MAX_COUNT;
        }

        public NetBuffer SentBuffer { get; }
        public PacketSequence packetSequence { get; } = 0;
        private UInt64 sendTimeStamp { get; set; } = 0;
        private PacketRetransmissionCount retransmissionCount = 0;

        private static readonly UInt64 RETRANSMISSION_TIMEOUT_MS = 32;
        private static readonly UInt64 RETRANSMISSION_MAX_COUNT = 16;
    }

    public class BufferStore
    {
        private SendBufferStore sendBufferStore = new SendBufferStore();

        public class SendBufferStore
        {
            PriorityQueue<SendPacketInfo, PacketSequence> sendBufferQueue = new PriorityQueue<SendPacketInfo, PacketSequence>(
                Comparer<PacketSequence>.Create((x, y) => x.CompareTo(y))
            );

            public void EnqueueSendBuffer(SendPacketInfo sendPacketInfo)
            {
                sendBufferQueue.Enqueue(sendPacketInfo, sendPacketInfo.packetSequence);
            }

            public SendPacketInfo? PeekSendBuffer()
            {
                return sendBufferQueue.Count == 0 ? null : sendBufferQueue.Peek();
            }

            public SendPacketInfo? DequeueSendBuffer()
            {
                return sendBufferQueue.Count == 0 ? null : sendBufferQueue.Dequeue();
            }
        }
    }
}
