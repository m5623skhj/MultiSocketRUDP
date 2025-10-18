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
        public SendPacketInfo(NetBuffer inSendedBuffer, PacketSequence inPacketSequence, UInt64 inSendTimeStamp)
        {
            sendedBuffer = inSendedBuffer;
            packetSequence = inPacketSequence;
            sendTimeStamp = inSendTimeStamp;
        }

        void RefreshSendPacketInfo(UInt64 now)
        {
            sendTimeStamp = now;
            ++retransmissionCount;
        }

        bool IsRetransmissionTime(UInt64 now)
        {
            return (now - sendTimeStamp) >= RETRANSMISSION_TIMEOUT_MS;
        }

        bool IsExceedMaxRetransmissionCount()
        {
            return retransmissionCount >= RETRANSMISSION_MAX_COUNT;
        }

        private NetBuffer sendedBuffer { get; }
        public PacketSequence packetSequence { get; }
        private UInt64 sendTimeStamp { get; set; }
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
