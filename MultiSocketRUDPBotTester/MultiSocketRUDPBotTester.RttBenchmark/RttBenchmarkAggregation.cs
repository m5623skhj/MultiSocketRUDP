using MultiSocketRUDPBotTester.Contents.Client;

namespace MultiSocketRUDPBotTester.RttBenchmark;

public static class RttBenchmarkAggregation
{
    public static RttBenchmarkAggregate Create(IReadOnlyList<RttTestSummary> runs)
    {
        if (runs.Count == 0)
        {
            throw new ArgumentException("At least one RTT run is required.", nameof(runs));
        }

        return new RttBenchmarkAggregate
        {
            MedianAverageRttMs = Median(runs.Select(run => run.AverageRttMs)),
            MedianMinRttMs = Median(runs.Select(run => run.MinRttMs)),
            MedianP50RttMs = Median(runs.Select(run => run.P50RttMs)),
            MedianP95RttMs = Median(runs.Select(run => run.P95RttMs)),
            MedianP99RttMs = Median(runs.Select(run => run.P99RttMs)),
            MedianMaxRttMs = Median(runs.Select(run => run.MaxRttMs)),
            WorstMaxRttMs = runs.Max(run => run.MaxRttMs),
            MedianElapsedSeconds = Median(runs.Select(run => run.ElapsedSeconds)),
            TotalRetransmissionSuspectedCount = runs.Sum(run => run.RetransmissionSuspectedCount)
        };
    }

    public static double Median(IEnumerable<double> values)
    {
        var sortedValues = values.Order().ToArray();
        if (sortedValues.Length == 0)
        {
            throw new ArgumentException("At least one value is required.", nameof(values));
        }

        var middleIndex = sortedValues.Length / 2;
        if (sortedValues.Length % 2 == 1)
        {
            return sortedValues[middleIndex];
        }

        return (sortedValues[middleIndex - 1] + sortedValues[middleIndex]) / 2.0;
    }
}
