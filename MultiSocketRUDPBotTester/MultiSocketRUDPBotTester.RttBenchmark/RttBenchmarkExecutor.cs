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
                Console.WriteLine(
                    $"[{options.ScenarioName}] warmup started: {options.WarmupSampleCount:N0} samples");
                var warmup = core.StartRttTest(
                    options.WarmupSampleCount,
                    options.TimeoutMs,
                    options.LossRate,
                    unchecked(options.SeedBase - 1));
                await WaitForRunAsync(warmup, options, "warmup");
                Console.WriteLine($"[{options.ScenarioName}] warmup completed");
            }

            var runs = new List<RttTestSummary>(options.RunCount);
            for (var runIndex = 0; runIndex < options.RunCount; ++runIndex)
            {
                var runNumber = runIndex + 1;
                Console.WriteLine(
                    $"[{options.ScenarioName}] run {runNumber}/{options.RunCount} started: " +
                    $"{options.SampleCount:N0} samples");
                var run = core.StartRttTest(
                    options.SampleCount,
                    options.TimeoutMs,
                    options.LossRate,
                    unchecked(options.SeedBase + runIndex));
                var summary = await WaitForRunAsync(run, options, $"run {runNumber}");
                runs.Add(summary);
                Console.WriteLine(
                    $"[{options.ScenarioName}] run {runNumber}/{options.RunCount} completed: " +
                    $"P95={summary.P95RttMs:F6} ms P99={summary.P99RttMs:F6} ms");
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
                RunTimeoutSeconds = options.RunTimeoutSeconds,
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

    private static async Task<RttTestSummary> WaitForRunAsync(
        Task<RttTestSummary> run,
        RttBenchmarkOptions options,
        string phase)
    {
        try
        {
            // A bounded wait keeps a stalled socket or server shutdown from consuming the whole CI job.
            return await run.WaitAsync(TimeSpan.FromSeconds(options.RunTimeoutSeconds));
        }
        catch (TimeoutException exception)
        {
            throw new TimeoutException(
                $"{options.ScenarioName} {phase} exceeded {options.RunTimeoutSeconds} seconds.",
                exception);
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
