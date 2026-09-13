using System.Diagnostics;
using MultiSocketRUDPBotTester.RttBenchmark;

namespace MultiSocketRUDPBotTester.UnitTests;

public sealed class ChannelRttTests
{
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
