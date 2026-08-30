using System.Diagnostics;

namespace MultiSocketRUDPBotTester.Contents.Client
{
    internal readonly record struct RttStressHistogramSnapshot(
        long SampleCount,
        double AverageMs,
        double MinMs,
        double P50Ms,
        double P95Ms,
        double P99Ms,
        double P999Ms,
        double MaxMs);

    internal sealed class RttStressHistogram
    {
        private const int BucketMicroseconds = 10;
        private const int MaximumTimeoutMs = 60000;

        private readonly long[] buckets;
        private long sampleCount;
        private long totalElapsedTicks;
        private long minElapsedTicks = long.MaxValue;
        private long maxElapsedTicks;

        public RttStressHistogram(int inTimeoutMs)
        {
            if (inTimeoutMs <= 0 || inTimeoutMs > MaximumTimeoutMs)
            {
                throw new ArgumentOutOfRangeException(
                    nameof(inTimeoutMs),
                    $"Timeout must be between 1 and {MaximumTimeoutMs} milliseconds.");
            }

            buckets = new long[checked(inTimeoutMs * 100 + 2)];
        }

        public void Record(long inElapsedTicks)
        {
            if (inElapsedTicks < 0)
            {
                throw new ArgumentOutOfRangeException(nameof(inElapsedTicks));
            }

            var elapsedMicroseconds = inElapsedTicks * 1000000.0 / Stopwatch.Frequency;
            var bucketIndex = Math.Min(
                buckets.Length - 1,
                (int)(elapsedMicroseconds / BucketMicroseconds));

            Interlocked.Increment(ref buckets[bucketIndex]);
            Interlocked.Increment(ref sampleCount);
            Interlocked.Add(ref totalElapsedTicks, inElapsedTicks);
            UpdateMinimum(inElapsedTicks);
            UpdateMaximum(inElapsedTicks);
        }

        public RttStressHistogramSnapshot CreateSnapshot()
        {
            var count = Interlocked.Read(ref sampleCount);
            if (count == 0)
            {
                return default;
            }

            var totalTicks = Interlocked.Read(ref totalElapsedTicks);
            var minTicks = Interlocked.Read(ref minElapsedTicks);
            var maxTicks = Interlocked.Read(ref maxElapsedTicks);
            return new RttStressHistogramSnapshot(
                count,
                TicksToMilliseconds(totalTicks) / count,
                TicksToMilliseconds(minTicks),
                PercentileMilliseconds(count, 50.0),
                PercentileMilliseconds(count, 95.0),
                PercentileMilliseconds(count, 99.0),
                PercentileMilliseconds(count, 99.9),
                TicksToMilliseconds(maxTicks));
        }

        private double PercentileMilliseconds(long inSampleCount, double inPercentile)
        {
            var rank = Math.Max(1L, (long)Math.Ceiling(inPercentile / 100.0 * inSampleCount));
            long accumulated = 0;
            for (var index = 0; index < buckets.Length; ++index)
            {
                accumulated += Interlocked.Read(ref buckets[index]);
                if (accumulated >= rank)
                {
                    return index * BucketMicroseconds / 1000.0;
                }
            }

            return (buckets.Length - 1) * BucketMicroseconds / 1000.0;
        }

        private void UpdateMinimum(long inElapsedTicks)
        {
            var current = Interlocked.Read(ref minElapsedTicks);
            while (inElapsedTicks < current)
            {
                var observed = Interlocked.CompareExchange(
                    ref minElapsedTicks,
                    inElapsedTicks,
                    current);
                if (observed == current)
                {
                    return;
                }

                current = observed;
            }
        }

        private void UpdateMaximum(long inElapsedTicks)
        {
            var current = Interlocked.Read(ref maxElapsedTicks);
            while (inElapsedTicks > current)
            {
                var observed = Interlocked.CompareExchange(
                    ref maxElapsedTicks,
                    inElapsedTicks,
                    current);
                if (observed == current)
                {
                    return;
                }

                current = observed;
            }
        }

        private static double TicksToMilliseconds(long inTicks)
        {
            return inTicks * 1000.0 / Stopwatch.Frequency;
        }
    }
}
