using MultiSocketRUDPBotTester.Contents.Client;

namespace MultiSocketRUDPBotTester.RttBenchmark;

public sealed class RttBenchmarkResult
{
    public int SchemaVersion { get; init; } = 1;
    public required string CommitSha { get; init; }
    public required DateTimeOffset RecordedAtUtc { get; init; }
    public required string ScenarioName { get; init; }
    public required double LossRate { get; init; }
    public string LossModel { get; init; } = "bot-tx-and-rx-independent";
    public required int SampleCount { get; init; }
    public required int RunCount { get; init; }
    public required int WarmupSampleCount { get; init; }
    public required int TimeoutMs { get; init; }
    public required int RunTimeoutSeconds { get; init; }
    public required int SeedBase { get; init; }
    public string ServerBuild { get; init; } = "Release /O2";
    public string BotTesterBuild { get; init; } = "Release / Optimize=true";
    public string IoWorkerSleepMode { get; init; } = "NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME";
    public required RttBenchmarkEnvironment Environment { get; init; }
    public required IReadOnlyList<RttTestSummary> Runs { get; init; }
    public required RttBenchmarkAggregate Aggregate { get; init; }
}

public sealed class RttBenchmarkEnvironment
{
    public required string OperatingSystem { get; init; }
    public required int ProcessorCount { get; init; }
    public string? ProcessorIdentifier { get; init; }
    public string? RunnerName { get; init; }
    public string? RunnerImage { get; init; }
}

public sealed class RttBenchmarkAggregate
{
    public required double MedianAverageRttMs { get; init; }
    public required double MedianMinRttMs { get; init; }
    public required double MedianP50RttMs { get; init; }
    public required double MedianP95RttMs { get; init; }
    public required double MedianP99RttMs { get; init; }
    public required double MedianMaxRttMs { get; init; }
    public required double WorstMaxRttMs { get; init; }
    public required double MedianElapsedSeconds { get; init; }
    public required int TotalRetransmissionSuspectedCount { get; init; }
}
