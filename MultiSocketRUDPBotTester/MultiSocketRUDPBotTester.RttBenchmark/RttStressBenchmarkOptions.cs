using System.Globalization;

namespace MultiSocketRUDPBotTester.RttBenchmark;

public sealed class RttStressBenchmarkOptions
{
    public const string Usage =
        "Usage: RttBenchmark stress [--host <ip>] [--port <port>] " +
        "[--clients <count>] [--warmup-seconds <seconds>] " +
        "[--measure-seconds <seconds>] [--timeout-ms <milliseconds>] " +
        "[--output <path>]";

    public string Host { get; init; } = "127.0.0.1";
    public ushort Port { get; init; } = 11011;
    public int ClientCount { get; init; } = 1000;
    public int WarmupSeconds { get; init; } = 30;
    public int MeasureSeconds { get; init; } = 300;
    public int TimeoutMs { get; init; } = 5000;
    public string OutputPath { get; init; } = "rtt-stress.json";

    public static RttStressBenchmarkOptions Parse(IReadOnlyList<string> inArguments)
    {
        if (inArguments.Count == 0 ||
            !string.Equals(inArguments[0], "stress", StringComparison.OrdinalIgnoreCase))
        {
            throw new ArgumentException("The first argument must be 'stress'.");
        }

        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        for (var index = 1; index < inArguments.Count; index += 2)
        {
            if (index + 1 >= inArguments.Count ||
                !inArguments[index].StartsWith("--", StringComparison.Ordinal))
            {
                throw new ArgumentException($"Invalid argument at position {index}.");
            }

            var name = inArguments[index][2..];
            if (!KnownOptionNames.Contains(name) || !values.TryAdd(name, inArguments[index + 1]))
            {
                throw new ArgumentException($"Unknown or duplicate option --{name}.");
            }
        }

        var host = GetValue(values, "host", "127.0.0.1");
        var outputPath = GetValue(values, "output", "rtt-stress.json");
        if (string.IsNullOrWhiteSpace(host))
        {
            throw new ArgumentException("--host cannot be empty.");
        }
        if (string.IsNullOrWhiteSpace(outputPath))
        {
            throw new ArgumentException("--output cannot be empty.");
        }

        return new RttStressBenchmarkOptions
        {
            Host = host,
            Port = ParseUShort(values, "port", 11011),
            ClientCount = ParseInt(values, "clients", 1000, 1, ushort.MaxValue),
            WarmupSeconds = ParseInt(values, "warmup-seconds", 30, 0, int.MaxValue),
            MeasureSeconds = ParseInt(values, "measure-seconds", 300, 1, int.MaxValue),
            TimeoutMs = ParseInt(values, "timeout-ms", 5000, 1, 60000),
            OutputPath = outputPath
        };
    }

    private static readonly HashSet<string> KnownOptionNames = new(
        [
            "host",
            "port",
            "clients",
            "warmup-seconds",
            "measure-seconds",
            "timeout-ms",
            "output"
        ],
        StringComparer.OrdinalIgnoreCase);

    private static string GetValue(
        IReadOnlyDictionary<string, string> inValues,
        string inName,
        string inDefaultValue)
    {
        return inValues.TryGetValue(inName, out var value) ? value : inDefaultValue;
    }

    private static int ParseInt(
        IReadOnlyDictionary<string, string> inValues,
        string inName,
        int inDefaultValue,
        int inMinimum,
        int inMaximum)
    {
        var value = GetValue(
            inValues,
            inName,
            inDefaultValue.ToString(CultureInfo.InvariantCulture));
        if (!int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed) ||
            parsed < inMinimum || parsed > inMaximum)
        {
            throw new ArgumentException(
                $"--{inName} must be between {inMinimum} and {inMaximum}.");
        }

        return parsed;
    }

    private static ushort ParseUShort(
        IReadOnlyDictionary<string, string> inValues,
        string inName,
        ushort inDefaultValue)
    {
        var parsed = ParseInt(
            inValues,
            inName,
            inDefaultValue,
            1,
            ushort.MaxValue);
        return (ushort)parsed;
    }
}
