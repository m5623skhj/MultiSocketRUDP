using MultiSocketRUDPBotTester.Contents.Client;

namespace MultiSocketRUDPBotTester.UnitTests;

public sealed class BotRttSampleCollectorTests
{
    /// <summary>
    /// 수집된 RTT 표본으로 평균, 분위수, 최댓값 및 재전송 의심 횟수를 계산하는지 확인합니다.
    /// </summary>
    [Fact]
    public void CreateSummaryMatchesRegularRttStatistics()
    {
        var collector = new BotRttSampleCollector();
        collector.RecordSample(10.0);
        collector.RecordSample(20.0);
        collector.RecordSample(40.0);
        collector.RecordSample(100.0);

        var summary = collector.CreateSummary();

        Assert.Equal(4, summary.SampleCount);
        Assert.Equal(42.5, summary.AverageRttMs);
        Assert.Equal(10.0, summary.MinRttMs);
        Assert.Equal(20.0, summary.P50RttMs);
        Assert.Equal(100.0, summary.P95RttMs);
        Assert.Equal(100.0, summary.P99RttMs);
        Assert.Equal(100.0, summary.MaxRttMs);
        Assert.Equal(2, summary.RetransmissionSuspectedCount);
        Assert.True(summary.ElapsedSeconds >= 0);
    }

    /// <summary>
    /// 표본이 없을 때 모든 RTT 요약값을 0으로 반환하는지 확인합니다.
    /// </summary>
    [Fact]
    public void EmptyCollectorReturnsZeroSampleSummary()
    {
        var summary = new BotRttSampleCollector().CreateSummary();

        Assert.Equal(0, summary.SampleCount);
        Assert.Equal(0, summary.AverageRttMs);
        Assert.Equal(0, summary.MinRttMs);
        Assert.Equal(0, summary.MaxRttMs);
    }

    /// <summary>
    /// 여러 스레드에서 동시에 기록한 RTT 표본이 누락 없이 요약에 포함되는지 확인합니다.
    /// </summary>
    [Fact]
    public void ConcurrentSamplesAreAllIncluded()
    {
        var collector = new BotRttSampleCollector();

        Parallel.For(0, 1_000, value => collector.RecordSample(value));

        var summary = collector.CreateSummary();
        Assert.Equal(1_000, summary.SampleCount);
        Assert.Equal(0, summary.MinRttMs);
        Assert.Equal(999, summary.MaxRttMs);
    }
}
