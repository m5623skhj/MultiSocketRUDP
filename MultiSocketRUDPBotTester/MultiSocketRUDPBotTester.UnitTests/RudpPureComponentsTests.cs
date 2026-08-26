using System.Text;
using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.ClientCore;
using MultiSocketRUDPBotTester.Contents.Client;

namespace MultiSocketRUDPBotTester.UnitTests;

public sealed class SessionBrokerResponseParserTests
{
    /// <summary>
    /// 세션 브로커 성공 응답에서 서버 주소, 세션 ID, 키 및 솔트를 모두 파싱하는지 확인합니다.
    /// </summary>
    [Fact]
    public void ParseReturnsEveryBrokerField()
    {
        var key = Enumerable.Range(1, SessionInfo.SessionKeySize).Select(value => (byte)value).ToArray();
        var salt = Enumerable.Range(21, SessionInfo.SessionSaltSize).Select(value => (byte)value).ToArray();
        var bytes = BuildResponse("127.0.0.1", 5000, 77, key, salt);

        var parsed = SessionBrokerResponseParser.Parse(bytes);

        Assert.Equal("127.0.0.1", parsed.ServerIp);
        Assert.Equal(5000, parsed.ServerPort);
        Assert.Equal(77, parsed.SessionId);
        Assert.Equal(key, parsed.SessionKey);
        Assert.Equal(salt, parsed.SessionSalt);
    }

    /// <summary>
    /// 브로커 오류 응답과 모든 길이의 잘린 성공 응답을 거부하는지 확인합니다.
    /// </summary>
    [Fact]
    public void ParseRejectsBrokerErrorAndEveryTruncatedSuccessResponse()
    {
        Assert.Throws<InvalidOperationException>(
            () => SessionBrokerResponseParser.Parse(
                new byte[] { (byte)ConnectResultCode.ServerFull }));

        var complete = BuildResponse(
            "127.0.0.1",
            5000,
            1,
            new byte[SessionInfo.SessionKeySize],
            new byte[SessionInfo.SessionSaltSize]);
        for (var length = 0; length < complete.Length; length++)
        {
            var truncated = complete[..length];
            Assert.Throws<InvalidDataException>(
                () => SessionBrokerResponseParser.Parse(truncated));
        }
    }

    private static byte[] BuildResponse(
        string ip,
        ushort port,
        ushort sessionId,
        byte[] key,
        byte[] salt)
    {
        var ipBytes = Encoding.UTF8.GetBytes(ip);
        var result = new List<byte>
        {
            (byte)ConnectResultCode.Success,
            (byte)(ipBytes.Length & 0xFF),
            (byte)(ipBytes.Length >> 8)
        };
        result.AddRange(ipBytes);
        AddUShort(result, port);
        AddUShort(result, sessionId);
        result.AddRange(key);
        result.AddRange(salt);
        return result.ToArray();
    }

    private static void AddUShort(List<byte> bytes, ushort value)
    {
        bytes.Add((byte)(value & 0xFF));
        bytes.Add((byte)(value >> 8));
    }
}

public sealed class DatagramFramerTests
{
    /// <summary>
    /// 유효한 시작 위치에서 페이로드 길이를 사용해 전체 데이터그램 크기를 계산하는지 확인합니다.
    /// </summary>
    [Fact]
    public void PacketSizeUsesPayloadLengthAtAnyValidOffset()
    {
        byte[] datagram = [9, 9, 0xCC, 4, 0, 0, 0, 1, 2, 3, 4];

        Assert.True(DatagramFramer.TryGetPacketSize(datagram, 2, out var size));
        Assert.Equal(9, size);
    }

    /// <summary>
    /// 잘못된 시작 위치 또는 길이가 0인 페이로드를 데이터그램 프레이밍에서 거부하는지 확인합니다.
    /// </summary>
    [Theory]
    [InlineData(-1)]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(5)]
    public void InvalidOffsetOrZeroPayloadIsRejected(int offset)
    {
        var data = new byte[5];

        Assert.False(DatagramFramer.TryGetPacketSize(data, offset, out var size));
        Assert.Equal(0, size);
    }
}

public sealed class ReceivePacketOrdererTests
{
    /// <summary>
    /// 시퀀스 갭을 보류했다가 순서대로 해제하고 중복 패킷을 무시하는지 확인합니다.
    /// </summary>
    [Fact]
    public void GapIsHeldThenReleasedInSequenceAndDuplicatesAreIgnored()
    {
        var orderer = new ReceivePacketOrderer();
        var second = Packet(2);
        var first = Packet(1);

        Assert.Empty(orderer.Collect(2, PacketId.TestPacketRes, second, PacketType.SendType));
        Assert.Equal(1, orderer.GetHeldPacketCount());

        var released = orderer.Collect(1, PacketId.TestPacketRes, first, PacketType.SendType);

        Assert.Equal(new ulong[] { 1, 2 }, released.Select(item => item.Item1));
        Assert.Same(first, released[0].Item3);
        Assert.Same(second, released[1].Item3);
        Assert.Equal(2UL, orderer.GetExpectedSequence());
        Assert.Equal(0, orderer.GetHeldPacketCount());
        Assert.Empty(orderer.Collect(2, PacketId.TestPacketRes, second, PacketType.SendType));
        Assert.Empty(orderer.Collect(1, PacketId.TestPacketRes, first, PacketType.SendType));
    }

    /// <summary>
    /// ulong 시퀀스 랩어라운드에서 보류한 0번 패킷을 올바른 순서로 해제하는지 확인합니다.
    /// </summary>
    [Fact]
    public void SequenceWrapReleasesHeldZeroAfterUlongMaximum()
    {
        var orderer = new ReceivePacketOrderer();
        orderer.SetExpectedSequenceForTest(ulong.MaxValue - 1);
        var wrapped = Packet(0);

        Assert.Empty(orderer.Collect(0, PacketId.Pong, wrapped, PacketType.SendType));
        var released = orderer.Collect(
            ulong.MaxValue,
            PacketId.Pong,
            Packet(ulong.MaxValue),
            PacketType.SendType);

        Assert.Equal(new ulong[] { ulong.MaxValue, 0 }, released.Select(item => item.Item1));
        Assert.Equal(0UL, orderer.GetExpectedSequence());
    }

    /// <summary>
    /// 패킷이 동시에 도착해도 결합된 결과의 순서와 유일성을 보존하는지 확인합니다.
    /// </summary>
    [Fact]
    public void ConcurrentArrivalPreservesJoinedOrderAndUniqueness()
    {
        var orderer = new ReceivePacketOrderer();
        var released = new System.Collections.Concurrent.ConcurrentBag<ulong>();

        Parallel.For(1, 501, sequence =>
        {
            foreach (var item in orderer.Collect(
                (ulong)sequence,
                PacketId.TestPacketRes,
                Packet((ulong)sequence),
                PacketType.SendType))
            {
                released.Add(item.Item1);
            }
        });

        Assert.Equal(500UL, orderer.GetExpectedSequence());
        Assert.Equal(0, orderer.GetHeldPacketCount());
        Assert.Equal(Enumerable.Range(1, 500).Select(value => (ulong)value), released.Order());
    }

    private static NetBuffer Packet(ulong sequence)
    {
        var buffer = new NetBuffer(16);
        buffer.WriteULong(sequence);
        return buffer;
    }
}

public sealed class PacketWaiterRegistryTests
{
    /// <summary>
    /// 같은 패킷 ID의 모든 대기자에게 완료를 전파하고 취소된 대기자를 정리하는지 확인합니다.
    /// </summary>
    [Fact]
    public async Task CompleteFansOutByPacketIdAndCancellationCleansRemainingWaiter()
    {
        var registry = new PacketWaiterRegistry();
        using var cancellation = new CancellationTokenSource();
        var firstPing = registry.WaitAsync(PacketId.Ping, 10_000, CancellationToken.None);
        var secondPing = registry.WaitAsync(PacketId.Ping, 10_000, CancellationToken.None);
        var pong = registry.WaitAsync(PacketId.Pong, 10_000, cancellation.Token);
        Assert.Equal(3, registry.GetPendingCount());
        var buffer = new NetBuffer(8);

        registry.Complete(PacketId.Ping, buffer);

        Assert.Same(buffer, await firstPing);
        Assert.Same(buffer, await secondPing);
        Assert.False(pong.IsCompleted);
        cancellation.Cancel();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => pong);
        Assert.Equal(0, registry.GetPendingCount());
    }

    /// <summary>
    /// 패킷 대기가 시간 초과되면 null을 반환하고 등록을 제거하는지 확인합니다.
    /// </summary>
    [Fact]
    public async Task TimeoutReturnsNullAndRemovesRegistration()
    {
        var registry = new PacketWaiterRegistry();

        var result = await registry.WaitAsync(PacketId.Ping, 1, CancellationToken.None);

        Assert.Null(result);
        Assert.Equal(0, registry.GetPendingCount());
    }

    /// <summary>
    /// 0 이하의 대기 시간을 등록 없이 거부하는지 확인합니다.
    /// </summary>
    [Fact]
    public void NonPositiveTimeoutIsRejectedWithoutRegistration()
    {
        var registry = new PacketWaiterRegistry();

        Assert.Throws<ArgumentOutOfRangeException>(
            () => registry.WaitAsync(PacketId.Ping, 0, CancellationToken.None).GetAwaiter().GetResult());
        Assert.Equal(0, registry.GetPendingCount());
    }
}

public sealed class RttStatisticsTests
{
    /// <summary>
    /// 빈 입력과 분위수 경계값에서 최근접 순위 방식으로 값을 계산하는지 확인합니다.
    /// </summary>
    [Fact]
    public void PercentileUsesNearestRankAcrossEmptyAndBoundaryInputs()
    {
        Assert.Equal(0.0, RttStatistics.Percentile([], 50));
        Assert.Equal(1.0, RttStatistics.Percentile([1, 2, 3, 4], 0));
        Assert.Equal(2.0, RttStatistics.Percentile([1, 2, 3, 4], 50));
        Assert.Equal(4.0, RttStatistics.Percentile([1, 2, 3, 4], 95));
        Assert.Equal(4.0, RttStatistics.Percentile([1, 2, 3, 4], 100));
        Assert.Throws<ArgumentOutOfRangeException>(
            () => RttStatistics.Percentile([1], -0.1));
        Assert.Throws<ArgumentOutOfRangeException>(
            () => RttStatistics.Percentile([1], 100.1));
    }

    /// <summary>
    /// 진행률 출력 정책이 보고 간격과 마지막 표본 경계를 올바르게 처리하는지 확인합니다.
    /// </summary>
    [Fact]
    public void ProgressPolicyUsesDetailedPrefixAndReportInterval()
    {
        Assert.False(RttStatistics.ShouldPrintProgress(0, 5, 100));
        Assert.True(RttStatistics.ShouldPrintProgress(5, 5, 100));
        Assert.False(RttStatistics.ShouldPrintProgress(6, 5, 100));
        Assert.True(RttStatistics.ShouldPrintProgress(100, 5, 100));
        Assert.Throws<ArgumentOutOfRangeException>(
            () => RttStatistics.ShouldPrintProgress(1, -1, 100));
        Assert.Throws<ArgumentOutOfRangeException>(
            () => RttStatistics.ShouldPrintProgress(1, 5, 0));
    }
}
