using System.Threading.Channels;

namespace MultiSocketRUDPBotTester.ClientCore;

/// <summary>가득 차면 가장 오래된 미송신 패킷을 버리고 새 패킷을 수용합니다.</summary>
internal sealed class UnreliablePacketQueue
{
    private readonly Channel<ReadOnlyMemory<byte>> packets;

    public UnreliablePacketQueue(int capacity = ProtocolConstants.UnreliableQueueCapacity)
    {
        packets = Channel.CreateBounded<ReadOnlyMemory<byte>>(new BoundedChannelOptions(capacity)
        {
            FullMode = BoundedChannelFullMode.DropOldest,
            SingleReader = true,
            SingleWriter = false,
            AllowSynchronousContinuations = false
        });
    }

    public bool Enqueue(ReadOnlyMemory<byte> packet) => packets.Writer.TryWrite(packet);
    public IAsyncEnumerable<ReadOnlyMemory<byte>> ReadAllAsync(CancellationToken token) =>
        packets.Reader.ReadAllAsync(token);
    public void Complete() => packets.Writer.TryComplete();
    public void Clear()
    {
        while (packets.Reader.TryRead(out _)) { }
    }
}

/// <summary>인증에 성공한 패킷만 단일 수신 작업에서 전달합니다. 번호 순환은 지원하지 않습니다.</summary>
internal sealed class LatestPacketSequence
{
    private bool hasReceived;
    private PacketSequence latest;

    public bool TryAccept(PacketSequence sequence)
    {
        if (hasReceived && sequence <= latest)
            return false;

        latest = sequence;
        hasReceived = true;
        return true;
    }
}
