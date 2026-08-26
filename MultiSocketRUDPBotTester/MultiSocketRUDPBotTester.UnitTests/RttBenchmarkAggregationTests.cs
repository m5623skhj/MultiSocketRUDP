using MultiSocketRUDPBotTester.Contents.Client;
using MultiSocketRUDPBotTester.RttBenchmark;

namespace MultiSocketRUDPBotTester.UnitTests;

public sealed class RttBenchmarkAggregationTests
{
    /// <summary>
    /// 짝수 개 값의 중앙값을 가운데 두 값의 평균으로 계산하는지 확인합니다.
    /// </summary>
    [Fact]
    public void MedianEvenNumberOfValuesReturnsMiddleAverage()
    {
        var median = RttBenchmarkAggregation.Median([4.0, 1.0, 3.0, 2.0]);

        Assert.Equal(2.5, median);
    }

    /// <summary>
    /// 반복 실행 결과의 안정 지표에는 중앙값을, 최악 지표에는 최댓값을 사용하는지 확인합니다.
    /// </summary>
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

public sealed class RttBenchmarkOptionsTests
{
    /// <summary>
    /// 양수인 실행 제한 시간을 명령줄 옵션에서 정상적으로 파싱하는지 확인합니다.
    /// </summary>
    [Fact]
    public void ParseAcceptsPositiveRunTimeout()
    {
        var options = RttBenchmarkOptions.Parse(CreateArguments("300"));

        Assert.Equal(300, options.RunTimeoutSeconds);
        Assert.Equal(1, options.ServerThreadCount);
    }

    /// <summary>
    /// 0 이하의 실행 제한 시간을 명령줄 옵션에서 거부하는지 확인합니다.
    /// </summary>
    [Fact]
    public void ParseRejectsNonPositiveRunTimeout()
    {
        Assert.Throws<ArgumentException>(() =>
            RttBenchmarkOptions.Parse(CreateArguments("0")));
    }

    private static string[] CreateArguments(string runTimeoutSeconds)
    {
        return
        [
            "run",
            "--host", "127.0.0.1",
            "--port", "11011",
            "--scenario", "Loss 0%",
            "--samples", "100",
            "--runs", "1",
            "--warmup-samples", "10",
            "--timeout-ms", "5000",
            "--run-timeout-seconds", runTimeoutSeconds,
            "--server-thread-count", "1",
            "--loss-rate", "0",
            "--seed-base", "1",
            "--commit", "abc123",
            "--output", "result.json"
        ];
    }
}
