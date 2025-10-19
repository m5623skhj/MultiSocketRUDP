using MultiSocketRUDPBotTester.Buffer;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace ClientCore
{
    public class SendPacketInfo
    {
        public SendPacketInfo(NetBuffer inSendedBuffer)
        {
            sendedBuffer = inSendedBuffer;
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

        public NetBuffer sendedBuffer { get; }
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
                if (sendBufferQueue.Count == 0)
                {
                    return null;
                }

                return sendBufferQueue.Peek();
            }

            public SendPacketInfo? DequeueSendBuffer()
            {
                if (sendBufferQueue.Count == 0)
                {
                    return null;
                }
                return sendBufferQueue.Dequeue();
            }
        }
    }
}
