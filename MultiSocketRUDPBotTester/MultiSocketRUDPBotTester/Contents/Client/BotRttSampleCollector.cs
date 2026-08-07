namespace MultiSocketRUDPBotTester.Contents.Client
{
    internal sealed class BotRttSampleCollector
    {
        private const double RetransmissionSuspectedThresholdMs = 40.0;

        private readonly Lock samplesLock = new();
        private readonly List<double> samples = [];
        private readonly long startedTimestamp = System.Diagnostics.Stopwatch.GetTimestamp();

        /// <summary>
        /// Records a Round Trip Time (RTT) sample in milliseconds.
        /// This method is thread-safe.
        /// </summary>
        /// <param name="inRttMs">The RTT sample in milliseconds. Must be non-negative, not NaN, and not infinity.</param>
        /// <exception cref="ArgumentOutOfRangeException">Thrown if <paramref name="inRttMs"/> is invalid.</exception>
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

        /// <summary>
        /// Creates a summary of the collected RTT samples, including count, average, min, max, and various percentiles.
        /// Samples are copied and sorted for calculation under a lock to ensure data consistency.
        /// If no samples are recorded, a summary with zero counts and only elapsed time is returned.
        /// </summary>
        /// <returns>An <see cref="RttTestSummary"/> containing the calculated statistics.</returns>
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
