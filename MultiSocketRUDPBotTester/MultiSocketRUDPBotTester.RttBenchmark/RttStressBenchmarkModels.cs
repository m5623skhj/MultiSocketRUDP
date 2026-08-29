using MultiSocketRUDPBotTester.Contents.Client;

namespace MultiSocketRUDPBotTester.RttBenchmark;

public sealed class RttStressBenchmarkResult
{
    public int SchemaVersion { get; init; } = 1;
    public required DateTimeOffset RecordedAtUtc { get; init; }
    public required RttBenchmarkEnvironment Environment { get; init; }
    public required int ThreadPoolMinWorkerThreads { get; init; }
    public required int ThreadPoolMinCompletionPortThreads { get; init; }
    public required RttStressTestSummary Summary { get; init; }
}
