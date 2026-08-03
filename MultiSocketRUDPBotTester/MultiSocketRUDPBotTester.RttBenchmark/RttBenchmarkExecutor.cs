using System.Runtime.InteropServices;
using MultiSocketRUDPBotTester.ClientCore;
using MultiSocketRUDPBotTester.Contents.Client;

namespace MultiSocketRUDPBotTester.RttBenchmark;

public static class RttBenchmarkExecutor
{
    public static async Task<RttBenchmarkResult> RunAsync(RttBenchmarkOptions options)
    {
        var core = BotTesterCore.Instance;
        core.SetConnectionInfo(options.Host, options.Port);

        try
        {
            if (options.WarmupSampleCount > 0)
            {
                await core.StartRttTest(
                    options.WarmupSampleCount,
                    options.TimeoutMs,
                    options.LossRate,
                    unchecked(options.SeedBase - 1));
            }

            var runs = new List<RttTestSummary>(options.RunCount);
            for (var runIndex = 0; runIndex < options.RunCount; ++runIndex)
            {
                runs.Add(await core.StartRttTest(
                    options.SampleCount,
                    options.TimeoutMs,
                    options.LossRate,
                    unchecked(options.SeedBase + runIndex)));
            }

            return new RttBenchmarkResult
            {
                CommitSha = options.CommitSha,
                RecordedAtUtc = DateTimeOffset.UtcNow,
                ScenarioName = options.ScenarioName,
                LossRate = options.LossRate,
                SampleCount = options.SampleCount,
                RunCount = options.RunCount,
                WarmupSampleCount = options.WarmupSampleCount,
                TimeoutMs = options.TimeoutMs,
                SeedBase = options.SeedBase,
                Environment = CaptureEnvironment(),
                Runs = runs,
                Aggregate = RttBenchmarkAggregation.Create(runs)
            };
        }
        finally
        {
            core.StopBotTest();
        }
    }

    private static RttBenchmarkEnvironment CaptureEnvironment()
    {
        return new RttBenchmarkEnvironment
        {
            OperatingSystem = RuntimeInformation.OSDescription,
            ProcessorCount = Environment.ProcessorCount,
            ProcessorIdentifier = Environment.GetEnvironmentVariable("PROCESSOR_IDENTIFIER"),
            RunnerName = Environment.GetEnvironmentVariable("RUNNER_NAME"),
            RunnerImage = Environment.GetEnvironmentVariable("ImageOS")
        };
    }
}
