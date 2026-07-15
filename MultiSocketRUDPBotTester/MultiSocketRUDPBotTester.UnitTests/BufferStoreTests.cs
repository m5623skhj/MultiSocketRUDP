using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.ClientCore;

namespace MultiSocketRUDPBotTester.UnitTests;

public sealed class BufferStoreTests
{
    /// <summary>
    /// SendPacketInfo가 생성·송신·ACK·제거 시각과 재전송 한계를 올바르게 추적하는지 확인합니다.
    /// </summary>
    [Fact]
    public void SendPacketInfoTracksTimestampsAndRetransmissionBoundaries()
    {
        var info = MakeInfo(7);

        info.InitializeSendTimestamp(100);
        info.InitializeSendTimestamp(110);
        Assert.Equal(100, info.GetCreatedTimestampMs());
        Assert.Equal(110, info.GetSendTimestampMs());
        Assert.False(info.IsRetransmissionTime(129));
        Assert.True(info.IsRetransmissionTime(130));

        info.MarkAckReceived(140);
        info.MarkAckReceived(150);
        info.MarkRemoved(160);
        Assert.True(info.HasAckReceived());
        Assert.Equal(140, info.GetAckReceivedTimestampMs());
        Assert.Equal(160, info.GetRemovedTimestampMs());

        for (var i = 0; i < 15; i++)
        {
            info.RefreshSendPacketInfo((ulong)(200 + i));
        }
        Assert.False(info.IsExceedMaxRetransmissionCount());
        info.RefreshSendPacketInfo(300);
        Assert.True(info.IsExceedMaxRetransmissionCount());
        Assert.Equal(16, info.GetRetransmissionCount());
        Assert.Equal(300, info.GetSendTimestampMs());
    }

    /// <summary>
    /// 여러 스레드의 재전송 갱신이 완료된 뒤 합산 횟수가 손실 없이 보존되는지 확인합니다.
    /// </summary>
    [Fact]
    public void SendPacketInfoConcurrentRefreshPreservesCountAfterJoin()
    {
        var info = MakeInfo(1);
        info.InitializeSendTimestamp(1);

        Parallel.For(0, 1_000, i => info.RefreshSendPacketInfo((ulong)(i + 2)));

        Assert.Equal(1_000, info.GetRetransmissionCount());
        Assert.InRange(info.GetSendTimestampMs(), 2, 1_001);
    }

    /// <summary>
    /// 송신 버퍼 저장소가 시퀀스 순서를 유지하고 중복 시퀀스의 항목을 교체하는지 확인합니다.
    /// </summary>
    [Fact]
    public void StoreUsesSequenceOrderAndReplacesDuplicateSequence()
    {
        var store = new BufferStore();
        var third = MakeInfo(3);
        var first = MakeInfo(1);
        var second = MakeInfo(2);
        var replacement = MakeInfo(2);

        store.EnqueueSendBuffer(third);
        store.EnqueueSendBuffer(first);
        store.EnqueueSendBuffer(second);
        store.EnqueueSendBuffer(replacement);

        Assert.Equal(3, store.GetSendBufferCount());
        Assert.Same(first, store.PeekSendBuffer());
        Assert.Same(first, store.DequeueSendBuffer());
        Assert.Same(replacement, store.GetSendBuffer(2));
        Assert.Equal(new ulong[] { 2, 3 }, store.GetAllSendPacketInfos().Select(i => i.PacketSequence));
    }

    /// <summary>
    /// 저장소의 삭제, snapshot 및 전체 정리가 항목 소유권과 개수를 올바르게 갱신하는지 확인합니다.
    /// </summary>
    [Fact]
    public void StoreRemoveSnapshotAndClearHaveExpectedOwnership()
    {
        var store = new BufferStore();
        var first = MakeInfo(1);
        var second = MakeInfo(2);
        store.EnqueueSendBuffer(first);
        store.EnqueueSendBuffer(second);

        var snapshot = store.GetAllSendPacketInfos();
        snapshot.Clear();
        Assert.Equal(2, store.GetSendBufferCount());
        Assert.Same(first, store.RemoveAndGetSendBuffer(1));
        Assert.Null(store.RemoveAndGetSendBuffer(1));
        Assert.False(store.ContainsPacket(1));
        store.RemoveSendBuffer(99);
        store.Clear();
        Assert.Equal(0, store.GetSendBufferCount());
        Assert.Null(store.PeekSendBuffer());
        Assert.Null(store.DequeueSendBuffer());
    }

    /// <summary>
    /// 고유 시퀀스의 병렬 추가와 삭제 후 개수와 시퀀스 유일성이 유지되는지 확인합니다.
    /// </summary>
    [Fact]
    public void ConcurrentUniqueEnqueueAndRemovePreserveJoinedCountAndUniqueness()
    {
        var store = new BufferStore();
        Parallel.For(0, 500, i => store.EnqueueSendBuffer(MakeInfo((ulong)i)));

        var snapshot = store.GetAllSendPacketInfos();
        Assert.Equal(500, snapshot.Count);
        Assert.Equal(500, snapshot.Select(i => i.PacketSequence).Distinct().Count());
        Assert.Equal(Enumerable.Range(0, 500).Select(i => (ulong)i), snapshot.Select(i => i.PacketSequence));

        Parallel.For(0, 250, i => store.RemoveSendBuffer((ulong)i));
        Assert.Equal(250, store.GetSendBufferCount());
    }

    private static SendPacketInfo MakeInfo(ulong sequence) => new(new NetBuffer(32), sequence);
}

public sealed class HoldingPacketStoreTests
{
    /// <summary>
    /// 빈 저장소가 첫 항목이나 범위 결과를 반환하지 않는지 확인합니다.
    /// </summary>
    [Fact]
    public void EmptyStoreReturnsNoFirstOrRange()
    {
        var store = new HoldingPacketStore();

        Assert.False(store.TryGetFirst(out var sequence, out var packet));
        Assert.Equal(0UL, sequence);
        Assert.Null(packet);
        Assert.False(store.TryGetRange(out var first, out var last));
        Assert.Equal(0UL, first);
        Assert.Equal(0UL, last);
    }

    /// <summary>
    /// 보류 패킷 저장소가 시퀀스 순서를 유지하고 중복 추가를 무시하는지 확인합니다.
    /// </summary>
    [Fact]
    public void StoreOrdersSequencesAndIgnoresDuplicateAdd()
    {
        var store = new HoldingPacketStore();
        var original = MakeHeld(2);
        var duplicate = MakeHeld(2);
        store.Add(3, MakeHeld(3));
        store.Add(2, original);
        store.Add(1, MakeHeld(1));
        store.Add(2, duplicate);

        Assert.True(store.TryGetFirst(out var sequence, out var packet));
        Assert.Equal(1UL, sequence);
        Assert.Equal(1UL, packet.Sequence);
        Assert.True(store.TryGetRange(out var first, out var last));
        Assert.Equal(1UL, first);
        Assert.Equal(3UL, last);
        store.Remove(1);
        Assert.True(store.TryGetFirst(out sequence, out packet));
        Assert.Equal(2UL, sequence);
        Assert.Same(original, packet);
    }

    /// <summary>
    /// 보류 패킷의 병렬 추가와 삭제가 완료된 뒤 저장소 불변식이 유지되는지 확인합니다.
    /// </summary>
    [Fact]
    public void ConcurrentUniqueAddsAndRemovesPreserveJoinedInvariants()
    {
        var store = new HoldingPacketStore();
        Parallel.For(0, 500, i => store.Add((ulong)i, MakeHeld((ulong)i)));

        Assert.Equal(500, store.GetCount());
        Assert.True(store.TryGetRange(out var first, out var last));
        Assert.Equal(0UL, first);
        Assert.Equal(499UL, last);

        Parallel.For(0, 250, i => store.Remove((ulong)i));
        Assert.Equal(250, store.GetCount());
        Assert.True(store.TryGetRange(out first, out last));
        Assert.Equal(250UL, first);
        Assert.Equal(499UL, last);
        store.Clear();
        Assert.Equal(0, store.GetCount());
    }

    private static HeldPacket MakeHeld(ulong sequence) => new()
    {
        Sequence = sequence,
        PacketId = PacketId.TestPacketReq,
        PacketType = PacketType.SendType,
        Buffer = new NetBuffer(32)
    };
}
