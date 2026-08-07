using MultiSocketRUDPBotTester.Contents.Client;

namespace MultiSocketRUDPBotTester.UnitTests;

public sealed class BotRttSampleCollectorTests
{
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

    [Fact]
    public void EmptyCollectorReturnsZeroSampleSummary()
    {
        var summary = new BotRttSampleCollector().CreateSummary();

        Assert.Equal(0, summary.SampleCount);
        Assert.Equal(0, summary.AverageRttMs);
        Assert.Equal(0, summary.MinRttMs);
        Assert.Equal(0, summary.MaxRttMs);
    }

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
