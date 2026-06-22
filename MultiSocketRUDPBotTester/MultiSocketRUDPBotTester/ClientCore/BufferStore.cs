using MultiSocketRUDPBotTester.Buffer;

public class SendPacketInfo(NetBuffer inSentBuffer, PacketSequence inPacketSequence)
{
    public NetBuffer SentBuffer { get; } = inSentBuffer;
    public PacketSequence PacketSequence { get; } = inPacketSequence;

    private long createdTimestampMs;
    private long sendTimeStampMs;
    private long ackReceivedTimestampMs;
    private long removedTimestampMs;
    private long retransmissionCount;

    private const long RetransmissionTimeoutMs = 20;
    private const long RetransmissionMaxCount = 16;

    public void InitializeSendTimestamp(ulong now)
    {
        Interlocked.CompareExchange(ref createdTimestampMs, (long)now, 0);
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

    public void MarkAckReceived(ulong now)
    {
        Interlocked.CompareExchange(ref ackReceivedTimestampMs, (long)now, 0);
    }

    public void MarkRemoved(ulong now)
    {
        Interlocked.Exchange(ref removedTimestampMs, (long)now);
    }

    public bool IsExceedMaxRetransmissionCount()
    {
        return Interlocked.Read(ref retransmissionCount) >= RetransmissionMaxCount;
    }

    public long GetRetransmissionCount()
    {
        return Interlocked.Read(ref retransmissionCount);
    }

    public long GetCreatedTimestampMs()
    {
        return Interlocked.Read(ref createdTimestampMs);
    }

    public long GetSendTimestampMs()
    {
        return Interlocked.Read(ref sendTimeStampMs);
    }

    public long GetAckReceivedTimestampMs()
    {
        return Interlocked.Read(ref ackReceivedTimestampMs);
    }

    public long GetRemovedTimestampMs()
    {
        return Interlocked.Read(ref removedTimestampMs);
    }

    public bool HasAckReceived()
    {
        return Interlocked.Read(ref ackReceivedTimestampMs) != 0;
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

    public SendPacketInfo? RemoveAndGetSendBuffer(PacketSequence sequence)
    {
        return sendBufferStore.RemoveAndGetSendBuffer(sequence);
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

    public SendPacketInfo? GetSendBuffer(PacketSequence sequence)
    {
        return sendBufferStore.GetSendBuffer(sequence);
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

        public SendPacketInfo? RemoveAndGetSendBuffer(PacketSequence sequence)
        {
            lock (sendBufferStoreLock)
            {
                if (!sendBufferStore.Remove(sequence, out var sendPacketInfo))
                {
                    return null;
                }

                return sendPacketInfo;
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

        public SendPacketInfo? GetSendBuffer(PacketSequence sequence)
        {
            lock (sendBufferStoreLock)
            {
                sendBufferStore.TryGetValue(sequence, out var sendPacketInfo);
                return sendPacketInfo;
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
