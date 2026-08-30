using System.Diagnostics;
using MultiSocketRUDPBotTester.Contents.Client;
using MultiSocketRUDPBotTester.RttBenchmark;

namespace MultiSocketRUDPBotTester.UnitTests;

public sealed class RttStressBenchmarkTests
{
    [Fact]
    public void StressOptionsUseComparisonDefaults()
    {
        var options = RttStressBenchmarkOptions.Parse(["stress"]);

        Assert.Equal("127.0.0.1", options.Host);
        Assert.Equal((ushort)11011, options.Port);
        Assert.Equal(1000, options.ClientCount);
        Assert.Equal(30, options.WarmupSeconds);
        Assert.Equal(300, options.MeasureSeconds);
        Assert.Equal(5000, options.TimeoutMs);
        Assert.Equal("rtt-stress.json", options.OutputPath);
    }

    [Fact]
    public void StressOptionsParseExplicitValues()
    {
        var options = RttStressBenchmarkOptions.Parse(
        [
            "stress",
            "--host", "10.0.0.5",
            "--port", "12000",
            "--clients", "25",
            "--warmup-seconds", "1",
            "--measure-seconds", "3",
            "--timeout-ms", "1000",
            "--output", "result.json"
        ]);

        Assert.Equal("10.0.0.5", options.Host);
        Assert.Equal((ushort)12000, options.Port);
        Assert.Equal(25, options.ClientCount);
        Assert.Equal(1, options.WarmupSeconds);
        Assert.Equal(3, options.MeasureSeconds);
        Assert.Equal(1000, options.TimeoutMs);
        Assert.Equal("result.json", options.OutputPath);
    }

    [Theory]
    [InlineData("--clients", "0")]
    [InlineData("--measure-seconds", "0")]
    [InlineData("--timeout-ms", "60001")]
    [InlineData("--unknown", "1")]
    public void StressOptionsRejectInvalidValues(string inName, string inValue)
    {
        Assert.Throws<ArgumentException>(() =>
            RttStressBenchmarkOptions.Parse(["stress", inName, inValue]));
    }

    [Fact]
    public void HistogramCalculatesNearestRankPercentiles()
    {
        var histogram = new RttStressHistogram(1000);
        histogram.Record(MillisecondsToTicks(1));
        histogram.Record(MillisecondsToTicks(2));
        histogram.Record(MillisecondsToTicks(3));
        histogram.Record(MillisecondsToTicks(4));

        var snapshot = histogram.CreateSnapshot();

        Assert.Equal(4, snapshot.SampleCount);
        Assert.InRange(snapshot.AverageMs, 2.49, 2.51);
        Assert.InRange(snapshot.MinMs, 0.99, 1.01);
        Assert.InRange(snapshot.P50Ms, 1.99, 2.01);
        Assert.InRange(snapshot.P95Ms, 3.99, 4.01);
        Assert.InRange(snapshot.P99Ms, 3.99, 4.01);
        Assert.InRange(snapshot.P999Ms, 3.99, 4.01);
        Assert.InRange(snapshot.MaxMs, 3.99, 4.01);
    }

    [Fact]
    public void HistogramSupportsConcurrentWriters()
    {
        var histogram = new RttStressHistogram(1000);
        var elapsedTicks = MillisecondsToTicks(5);

        Parallel.For(0, 10000, _ => histogram.Record(elapsedTicks));
        var snapshot = histogram.CreateSnapshot();

        Assert.Equal(10000, snapshot.SampleCount);
        Assert.InRange(snapshot.P50Ms, 4.99, 5.01);
        Assert.InRange(snapshot.P99Ms, 4.99, 5.01);
    }

    [Fact]
    public void StressPayloadMatchesComparisonPayloadSize()
    {
        Assert.Equal(32, Client.RttStressApplicationPayloadBytes);
    }

    private static long MillisecondsToTicks(int inMilliseconds)
    {
        return inMilliseconds * Stopwatch.Frequency / 1000;
    }
}
