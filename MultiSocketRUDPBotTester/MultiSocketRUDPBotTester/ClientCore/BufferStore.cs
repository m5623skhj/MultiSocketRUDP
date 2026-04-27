using MultiSocketRUDPBotTester.Buffer;

public class SendPacketInfo(NetBuffer inSentBuffer, PacketSequence inPacketSequence)
{
    public NetBuffer SentBuffer { get; } = inSentBuffer;
    public PacketSequence PacketSequence { get; } = inPacketSequence;

    private long sendTimeStampMs;
    private long retransmissionCount;

    private const long RetransmissionTimeoutMs = 50;
    private const long RetransmissionMaxCount = 16;

    public void InitializeSendTimestamp(ulong now)
    {
        Interlocked.Exchange(ref sendTimeStampMs, (long)now);
    }

    public void RefreshSendPacketInfo(ulong now)
    {
        Interlocked.Exchange(ref sendTimeStampMs, (long)now);
        Interlocked.Increment(ref retransmissionCount);
    }

    public bool IsRetransmissionTime(ulong now)
    {
        var stamp = (ulong)Interlocked.Read(ref sendTimeStampMs);
        return (now - stamp) >= (ulong)RetransmissionTimeoutMs;
    }

    public bool IsExceedMaxRetransmissionCount()
    {
        return Interlocked.Read(ref retransmissionCount) >= RetransmissionMaxCount;
    }

    public long GetRetransmissionCount()
    {
        return Interlocked.Read(ref retransmissionCount);
    }
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

    public bool ContainsPacket(PacketSequence sequence)
    {
        return sendBufferStore.ContainsPacket(sequence);
    }

    public void Clear()
    {
        sendBufferStore.Clear();
    }

    public class SendBufferStore
    {
        private readonly SortedDictionary<PacketSequence, SendPacketInfo> sendBufferStore = new();
        private readonly Lock sendBufferStoreLock = new();

        public void EnqueueSendBuffer(SendPacketInfo sendPacketInfo)
        {
            lock (sendBufferStoreLock)
            {
                if (sendBufferStore.TryAdd(sendPacketInfo.PacketSequence, sendPacketInfo))
                {
                    return;
                }

                Serilog.Log.Debug("Updating existing packet sequence: {Seq}", sendPacketInfo.PacketSequence);
                sendBufferStore[sendPacketInfo.PacketSequence] = sendPacketInfo;
            }
        }

        public SendPacketInfo? PeekSendBuffer()
        {
            lock (sendBufferStoreLock)
            {
                return sendBufferStore.Count == 0 ? null : sendBufferStore.First().Value;
            }
        }

        public SendPacketInfo? DequeueSendBuffer()
        {
            lock (sendBufferStoreLock)
            {
                if (sendBufferStore.Count == 0)
                {
                    return null;
                }

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

        public bool ContainsPacket(PacketSequence sequence)
        {
            lock (sendBufferStoreLock)
            {
                return sendBufferStore.ContainsKey(sequence);
            }
        }

        public void Clear()
        {
            lock (sendBufferStoreLock)
            {
                sendBufferStore.Clear();
            }
        }
    }
}
