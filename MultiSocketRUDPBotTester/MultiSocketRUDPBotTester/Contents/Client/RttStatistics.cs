namespace MultiSocketRUDPBotTester.Contents.Client
{
    internal static class RttStatistics
    {
        public static double Percentile(IReadOnlyList<double> sortedSamples, double percentile)
        {
            if (sortedSamples.Count == 0)
            {
                return 0;
            }

            if (percentile is < 0 or > 100)
            {
                throw new ArgumentOutOfRangeException(nameof(percentile));
            }

            var rank = (int)Math.Ceiling(percentile / 100.0 * sortedSamples.Count);
            var index = Math.Clamp(rank - 1, 0, sortedSamples.Count - 1);
            return sortedSamples[index];
        }

        public static bool ShouldPrintProgress(
            int sampleCount,
            int detailedSampleCount,
            int reportInterval)
        {
            if (sampleCount <= 0)
            {
                return false;
            }

            if (detailedSampleCount < 0)
            {
                throw new ArgumentOutOfRangeException(nameof(detailedSampleCount));
            }

            if (reportInterval <= 0)
            {
                throw new ArgumentOutOfRangeException(nameof(reportInterval));
            }

            return sampleCount <= detailedSampleCount || sampleCount % reportInterval == 0;
        }
    }
}
