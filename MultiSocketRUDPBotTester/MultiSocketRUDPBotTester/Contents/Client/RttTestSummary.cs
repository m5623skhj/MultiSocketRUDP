namespace MultiSocketRUDPBotTester.Contents.Client
{
    public sealed class RttTestSummary
    {
        public int SampleCount { get; init; }
        public double AverageRttMs { get; init; }
        public double MinRttMs { get; init; }
        public double MaxRttMs { get; init; }
        public double ElapsedSeconds { get; init; }
    }
}
