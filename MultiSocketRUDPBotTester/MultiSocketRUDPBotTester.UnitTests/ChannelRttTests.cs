using System.Diagnostics;
using MultiSocketRUDPBotTester.RttBenchmark;

namespace MultiSocketRUDPBotTester.UnitTests;

public sealed class ChannelRttTests
{
    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(13)]
    [InlineData(100)]
    [InlineData(1000)]
    public void StandaloneChannelsSendTheSameNumberOfSingleRequestBatches(int count)
    {
        var reliable = ChannelRttBenchmark.GetBatches("reliable-baseline", count).ToArray();
        var unreliable = ChannelRttBenchmark.GetBatches("unreliable-only", count).ToArray();
        Assert.Equal(count, reliable.Length);
        Assert.Equal(count, unreliable.Length);
        Assert.All(reliable, batch => Assert.Equal((1, 0), batch));
        Assert.All(unreliable, batch => Assert.Equal((0, 1), batch));
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public async Task ResponseBeforeWaitingCompletesImmediatelyForEitherChannel(bool unreliable)
    {
        var samples = new ChannelSamples(1000);
        var start = Stopwatch.GetTimestamp();
        samples.Begin(1, unreliable, start);
        samples.Admission(1, unreliable, true);
        samples.Receive(1, unreliable, start + 1);
        Assert.True(await samples.WaitForResponseAsync(1, CancellationToken.None));
        Assert.Equal(1, samples.Snapshot(unreliable, 1).Received);
    }

    [Fact]
    public async Task MissingResponseExpiresWhileWaiting()
    {
        var samples = new ChannelSamples(20);
        samples.Begin(1, true, Stopwatch.GetTimestamp());
        samples.Admission(1, true, true);
        Assert.False(await samples.WaitForResponseAsync(1, CancellationToken.None)
            .WaitAsync(TimeSpan.FromSeconds(5)));
        Assert.Equal(1, samples.Snapshot(true, 1).TimedOut);
        Assert.Null(samples.Snapshot(true, 1).P95Ms);
    }

    [Fact]
    public async Task MissingResponseTimesOutAndLateReplyCannotCompleteTheNextRequest()
    {
        var samples = new ChannelSamples(1000);
        samples.Begin(1, true, Stopwatch.GetTimestamp() - 2 * Stopwatch.Frequency);
        samples.Admission(1, true, true);
        Assert.False(await samples.WaitForResponseAsync(1, CancellationToken.None));

        var nextStart = Stopwatch.GetTimestamp();
        samples.Begin(2, true, nextStart);
        samples.Admission(2, true, true);
        var next = samples.WaitForResponseAsync(2, CancellationToken.None);
        samples.Receive(1, true, Stopwatch.GetTimestamp());
        samples.Receive(1, true, Stopwatch.GetTimestamp());
        Assert.False(next.IsCompleted);
        samples.Receive(2, true, Stopwatch.GetTimestamp());
        Assert.True(await next);
        var summary = samples.Snapshot(true, 1);
        Assert.Equal(1, summary.TimedOut);
        Assert.Equal(1, summary.Late);
        Assert.Equal(1, summary.Received);
        Assert.Equal(50, summary.DeliveryPercent);
    }

    [Fact]
    public async Task WaitingStopsOnCancellationWithoutCountingPacketLoss()
    {
        var samples = new ChannelSamples(10000);
        samples.Begin(1, true, Stopwatch.GetTimestamp());
        samples.Admission(1, true, true);
        using var cancellation = new CancellationTokenSource();
        var waiting = samples.WaitForResponseAsync(1, cancellation.Token);
        cancellation.Cancel();
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => waiting);
        Assert.Equal(0, samples.Snapshot(true, 1).TimedOut);
    }

    [Fact]
    public void MixedLoadKeepsTenToOneBatchesAndHandlesPartialFinalBatch()
    {
        Assert.Equal(new[] { (1, 10), (1, 3) },
            ChannelRttBenchmark.GetBatches("mixed", 13).ToArray());
    }

    [Fact]
    public void MatchesIdsAcrossReorderingAndIgnoresDuplicatesWrongChannelsAndUnknownIds()
    {
        var samples = new ChannelSamples(1000);
        var start = Stopwatch.GetTimestamp();
        var tenMs = Stopwatch.Frequency / 100;
        samples.Begin(1, true, start);
        samples.Begin(2, true, start + tenMs);
        samples.Admission(1, true, true);
        samples.Admission(2, true, true);
        samples.Receive(2, false, start + tenMs * 2);
        samples.Receive(999, true, start + tenMs * 2);
        samples.Receive(2, true, start + tenMs * 2);
        samples.Receive(1, true, start + tenMs * 3);
        samples.Receive(1, true, start + tenMs * 4);
        var summary = samples.Snapshot(true, 1);
        Assert.Equal(2, summary.Received);
        Assert.Equal(100, summary.DeliveryPercent);
        Assert.Equal(10, summary.P50Ms!.Value, 3);
        Assert.Equal(30, summary.P99Ms!.Value, 3);
        Assert.Equal(0, samples.Snapshot(false, 1).Received);
    }

    [Fact]
    public void MissingLateAndRejectedRequestsReduceDeliveryAndDoNotBecomeZeroRttSamples()
    {
        var samples = new ChannelSamples(100);
        var start = Stopwatch.GetTimestamp();
        for (ulong id = 1; id <= 3; id++)
        {
            samples.Begin(id, true, start);
            samples.Admission(id, true, id != 3);
        }
        samples.Receive(1, true, start + Stopwatch.Frequency);
        samples.Receive(3, true, start + 1);
        var summary = samples.Snapshot(true, 2);
        Assert.Equal(3, summary.Attempted);
        Assert.Equal(2, summary.Accepted);
        Assert.Equal(1, summary.Late);
        Assert.Equal(0, summary.Received);
        Assert.Equal(0, summary.DeliveryPercent);
        Assert.Null(summary.P95Ms);
    }
}
