using MultiSocketRUDPBotTester.Buffer;
using static ClientCore.BufferStore;

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
            SendTimeStamp = now;
            ++retransmissionCount;
        }

        public bool IsRetransmissionTime(UInt64 now)
        {
            return (now - SendTimeStamp) >= RetransmissionTimeoutMs;
        }

        public bool IsExceedMaxRetransmissionCount()
        {
            return retransmissionCount >= RetransmissionMaxCount;
        }

        public NetBuffer SentBuffer { get; }
        public PacketSequence packetSequence => 0;
        private ulong SendTimeStamp { get; set; } = 0;
        private PacketRetransmissionCount retransmissionCount = 0;

        private const ulong RetransmissionTimeoutMs = 32;
        private const ulong RetransmissionMaxCount = 16;
    }

    public class BufferStore
    {
        private readonly SendBufferStore sendBufferStore = new();

        public void EnqueueSendBuffer(SendPacketInfo sendPacketInfo)
        {
            sendBufferStore.EnqueueSendBuffer(sendPacketInfo);
        }

        public SendPacketInfo? PeekSendBuffer()
        {
            return sendBufferStore.PeekSendBuffer();
        }

        public SendPacketInfo? DequeueSendBuffer()
        {
            return sendBufferStore.DequeueSendBuffer();
        }

        public void RemoveSendBuffer(PacketSequence sequence)
        {
            sendBufferStore.RemoveSendBuffer(sequence);
        }

        public int GetSendBufferCount()
        {
            return sendBufferStore.GetSendBufferCount();
        }

        public List<SendPacketInfo> GetAllSendPacketInfos()
        {
            return sendBufferStore.GetAllSendPacketInfos();
        }

        public class SendBufferStore
        {
            private readonly SortedDictionary<PacketSequence, SendPacketInfo> sendBufferStore = new();
            private readonly Lock sendBufferStoreLock = new();

            public void EnqueueSendBuffer(SendPacketInfo sendPacketInfo)
            {
                lock (sendBufferStoreLock)
                {
                    sendBufferStore.Add(sendPacketInfo.packetSequence, sendPacketInfo);
                }
            }

            public SendPacketInfo? PeekSendBuffer()
            {
                lock (sendBufferStoreLock)
                {
                    return sendBufferStore.First().Value;
                }
            }

            public SendPacketInfo? DequeueSendBuffer()
            {
                lock (sendBufferStoreLock)
                {
                    var firstKey = sendBufferStore.First().Key;
                    var firstValue = sendBufferStore[firstKey];
                    sendBufferStore.Remove(firstKey);
                    
                    return firstValue;
                }
            }

            public void RemoveSendBuffer(PacketSequence sequence)
            {
                lock (sendBufferStoreLock)
                {
                    sendBufferStore.Remove(sequence);
                }
            }

            public int GetSendBufferCount()
            {
                lock (sendBufferStoreLock)
                {
                    return sendBufferStore.Count;
                }
            }

            public List<SendPacketInfo> GetAllSendPacketInfos()
            {
                lock (sendBufferStoreLock)
                {
                    return sendBufferStore.Values.ToList();
                }
            }
        }
    }
}
