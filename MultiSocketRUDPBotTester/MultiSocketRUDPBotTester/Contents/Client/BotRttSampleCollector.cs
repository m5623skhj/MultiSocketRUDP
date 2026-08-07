namespace MultiSocketRUDPBotTester.Contents.Client
{
    internal sealed class BotRttSampleCollector
    {
        private const double RetransmissionSuspectedThresholdMs = 40.0;

        private readonly Lock samplesLock = new();
        private readonly List<double> samples = [];
        private readonly long startedTimestamp = System.Diagnostics.Stopwatch.GetTimestamp();

        public void RecordSample(double inRttMs)
        {
            if (inRttMs < 0 || double.IsNaN(inRttMs) || double.IsInfinity(inRttMs))
            {
                throw new ArgumentOutOfRangeException(nameof(inRttMs));
            }

            lock (samplesLock)
            {
                samples.Add(inRttMs);
            }
        }

        public RttTestSummary CreateSummary()
        {
            List<double> sortedSamples;
            lock (samplesLock)
            {
                sortedSamples = [.. samples];
            }

            sortedSamples.Sort();
            if (sortedSamples.Count == 0)
            {
                return new RttTestSummary
                {
                    ElapsedSeconds = System.Diagnostics.Stopwatch
                        .GetElapsedTime(startedTimestamp)
                        .TotalSeconds
                };
            }

            return new RttTestSummary
            {
                SampleCount = sortedSamples.Count,
                AverageRttMs = sortedSamples.Average(),
                MinRttMs = sortedSamples[0],
                MaxRttMs = sortedSamples[^1],
                P50RttMs = RttStatistics.Percentile(sortedSamples, 50.0),
                P95RttMs = RttStatistics.Percentile(sortedSamples, 95.0),
                P99RttMs = RttStatistics.Percentile(sortedSamples, 99.0),
                RetransmissionSuspectedCount = sortedSamples.Count(
                    rtt => rtt >= RetransmissionSuspectedThresholdMs),
                ElapsedSeconds = System.Diagnostics.Stopwatch
                    .GetElapsedTime(startedTimestamp)
                    .TotalSeconds
            };
        }
    }
}
