using MultiSocketRUDPBotTester.Contents.Client;
using MultiSocketRUDPBotTester.RttBenchmark;

namespace MultiSocketRUDPBotTester.UnitTests;

public sealed class RttBenchmarkAggregationTests
{
    [Fact]
    public void MedianEvenNumberOfValuesReturnsMiddleAverage()
    {
        var median = RttBenchmarkAggregation.Median([4.0, 1.0, 3.0, 2.0]);

        Assert.Equal(2.5, median);
    }

    [Fact]
    public void CreateUsesMedianForStableMetricsAndWorstValueForMaximum()
    {
        var runs = new[]
        {
            CreateSummary(average: 1.0, p95: 2.0, p99: 3.0, maximum: 8.0, retransmissionCount: 1),
            CreateSummary(average: 2.0, p95: 3.0, p99: 4.0, maximum: 20.0, retransmissionCount: 2),
            CreateSummary(average: 100.0, p95: 4.0, p99: 5.0, maximum: 12.0, retransmissionCount: 3)
        };

        var aggregate = RttBenchmarkAggregation.Create(runs);

        Assert.Equal(2.0, aggregate.MedianAverageRttMs);
        Assert.Equal(3.0, aggregate.MedianP95RttMs);
        Assert.Equal(4.0, aggregate.MedianP99RttMs);
        Assert.Equal(12.0, aggregate.MedianMaxRttMs);
        Assert.Equal(20.0, aggregate.WorstMaxRttMs);
        Assert.Equal(6, aggregate.TotalRetransmissionSuspectedCount);
    }

    private static RttTestSummary CreateSummary(
        double average,
        double p95,
        double p99,
        double maximum,
        int retransmissionCount)
    {
        return new RttTestSummary
        {
            SampleCount = 100,
            AverageRttMs = average,
            MinRttMs = 0.1,
            MaxRttMs = maximum,
            P50RttMs = 0.5,
            P95RttMs = p95,
            P99RttMs = p99,
            RetransmissionSuspectedCount = retransmissionCount,
            LossRate = 0.0,
            LossSeed = 1,
            ElapsedSeconds = 1.0
        };
    }
}
