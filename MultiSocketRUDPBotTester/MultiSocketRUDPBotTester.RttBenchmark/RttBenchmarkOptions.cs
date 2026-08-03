using System.Globalization;

namespace MultiSocketRUDPBotTester.RttBenchmark;

public sealed class RttBenchmarkOptions
{
    public const string Usage =
        "Usage: RttBenchmark --host <ip> --port <port> --scenario <name> " +
        "--samples <count> --runs <count> --warmup-samples <count> " +
        "--timeout-ms <milliseconds> --run-timeout-seconds <seconds> " +
        "--server-thread-count <count> --loss-rate <0..1> --seed-base <integer> " +
        "--commit <sha> --output <path>";

    public required string Host { get; init; }
    public required ushort Port { get; init; }
    public required string ScenarioName { get; init; }
    public required int SampleCount { get; init; }
    public required int RunCount { get; init; }
    public required int WarmupSampleCount { get; init; }
    public required int TimeoutMs { get; init; }
    public required int RunTimeoutSeconds { get; init; }
    public required int ServerThreadCount { get; init; }
    public required double LossRate { get; init; }
    public required int SeedBase { get; init; }
    public required string CommitSha { get; init; }
    public required string OutputPath { get; init; }

    public static RttBenchmarkOptions Parse(IReadOnlyList<string> args)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        var startIndex = args.Count > 0 && string.Equals(args[0], "run", StringComparison.OrdinalIgnoreCase) ? 1 : 0;
        for (var index = startIndex; index < args.Count; index += 2)
        {
            if (index + 1 >= args.Count || !args[index].StartsWith("--", StringComparison.Ordinal))
            {
                throw new ArgumentException($"Invalid argument at position {index}.");
            }

            values[args[index][2..]] = args[index + 1];
        }

        var host = GetRequired(values, "host");
        var scenarioName = GetRequired(values, "scenario");
        var commitSha = GetRequired(values, "commit");
        var outputPath = GetRequired(values, "output");
        var port = ParseUShort(values, "port");
        var sampleCount = ParseInt(values, "samples", minimum: 1);
        var runCount = ParseInt(values, "runs", minimum: 1);
        var warmupSampleCount = ParseInt(values, "warmup-samples", minimum: 0);
        var timeoutMs = ParseInt(values, "timeout-ms", minimum: 1);
        var runTimeoutSeconds = ParseInt(values, "run-timeout-seconds", minimum: 1);
        var serverThreadCount = ParseInt(values, "server-thread-count", minimum: 1);
        var seedBase = ParseInt(values, "seed-base", int.MinValue);
        var lossRate = ParseDouble(values, "loss-rate");
        if (lossRate is < 0.0 or >= 1.0)
        {
            throw new ArgumentOutOfRangeException(nameof(lossRate), "Loss rate must satisfy 0 <= rate < 1.");
        }

        return new RttBenchmarkOptions
        {
            Host = host,
            Port = port,
            ScenarioName = scenarioName,
            SampleCount = sampleCount,
            RunCount = runCount,
            WarmupSampleCount = warmupSampleCount,
            TimeoutMs = timeoutMs,
            RunTimeoutSeconds = runTimeoutSeconds,
            ServerThreadCount = serverThreadCount,
            LossRate = lossRate,
            SeedBase = seedBase,
            CommitSha = commitSha,
            OutputPath = outputPath
        };
    }

    private static string GetRequired(IReadOnlyDictionary<string, string> values, string name)
    {
        if (!values.TryGetValue(name, out var value) || string.IsNullOrWhiteSpace(value))
        {
            throw new ArgumentException($"Missing required argument --{name}.");
        }

        return value;
    }

    private static int ParseInt(IReadOnlyDictionary<string, string> values, string name, int minimum)
    {
        var value = GetRequired(values, name);
        if (!int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed) || parsed < minimum)
        {
            throw new ArgumentException($"--{name} must be an integer greater than or equal to {minimum}.");
        }

        return parsed;
    }

    private static ushort ParseUShort(IReadOnlyDictionary<string, string> values, string name)
    {
        var value = GetRequired(values, name);
        if (!ushort.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed) || parsed == 0)
        {
            throw new ArgumentException($"--{name} must be between 1 and 65535.");
        }

        return parsed;
    }

    private static double ParseDouble(IReadOnlyDictionary<string, string> values, string name)
    {
        var value = GetRequired(values, name);
        if (!double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out var parsed))
        {
            throw new ArgumentException($"--{name} must be a number.");
        }

        return parsed;
    }
}
