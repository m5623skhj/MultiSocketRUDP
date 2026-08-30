using System.Runtime.InteropServices;
using MultiSocketRUDPBotTester.ClientCore;
using MultiSocketRUDPBotTester.Contents.Client;

namespace MultiSocketRUDPBotTester.RttBenchmark;

public static class RttStressBenchmarkExecutor
{
    public static async Task<RttStressBenchmarkResult> RunAsync(
        RttStressBenchmarkOptions inOptions,
        CancellationToken inCancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(inOptions);
        var core = BotTesterCore.Instance;
        core.SetConnectionInfo(inOptions.Host, inOptions.Port);
        ThreadPool.GetMinThreads(
            out var originalMinWorkerThreads,
            out var originalMinCompletionPortThreads);
        var stressMinimumThreads = Math.Min(
            inOptions.ClientCount,
            Math.Max(32, Environment.ProcessorCount * 8));
        var stressMinWorkerThreads = Math.Max(
            originalMinWorkerThreads,
            stressMinimumThreads);
        var stressMinCompletionPortThreads = Math.Max(
            originalMinCompletionPortThreads,
            stressMinimumThreads);
        if (!ThreadPool.SetMinThreads(
            stressMinWorkerThreads,
            stressMinCompletionPortThreads))
        {
            throw new InvalidOperationException("Failed to configure the stress-test ThreadPool.");
        }

        try
        {
            Console.WriteLine(
                $"MultiSocketRUDP stress connecting: clients={inOptions.ClientCount} " +
                $"warmup={inOptions.WarmupSeconds} measure={inOptions.MeasureSeconds} " +
                $"timeout={inOptions.TimeoutMs}");
            Console.WriteLine(
                $"ThreadPool minimums: workers={stressMinWorkerThreads} " +
                $"completion_ports={stressMinCompletionPortThreads}");

            var summary = await core.StartRttStressTest(
                new RttStressTestConfiguration
                {
                    ClientCount = inOptions.ClientCount,
                    WarmupSeconds = inOptions.WarmupSeconds,
                    MeasureSeconds = inOptions.MeasureSeconds,
                    TimeoutMs = inOptions.TimeoutMs
                },
                inCancellationToken).ConfigureAwait(false);

            PrintSummary(summary);
            return new RttStressBenchmarkResult
            {
                RecordedAtUtc = DateTimeOffset.UtcNow,
                Environment = CaptureEnvironment(),
                ThreadPoolMinWorkerThreads = stressMinWorkerThreads,
                ThreadPoolMinCompletionPortThreads = stressMinCompletionPortThreads,
                Summary = summary
            };
        }
        finally
        {
            core.StopBotTest();
            ThreadPool.SetMinThreads(
                originalMinWorkerThreads,
                originalMinCompletionPortThreads);
        }
    }

    private static void PrintSummary(RttStressTestSummary inSummary)
    {
        Console.WriteLine();
        Console.WriteLine("=== BENCHMARK_RESULT ===");
        Console.WriteLine($"library={inSummary.Library}");
        Console.WriteLine($"mode={inSummary.Mode}");
        Console.WriteLine($"application_payload_bytes={inSummary.ApplicationPayloadBytes}");
        Console.WriteLine($"target_clients={inSummary.TargetClients}");
        Console.WriteLine($"connected_clients={inSummary.ConnectedClients}");
        Console.WriteLine($"connection_failures={inSummary.ConnectionFailures}");
        Console.WriteLine($"runtime_disconnects={inSummary.RuntimeDisconnects}");
        Console.WriteLine(
            $"retransmission_limit_disconnects={inSummary.RetransmissionLimitDisconnects}");
        Console.WriteLine(
            $"server_unresponsive_disconnects={inSummary.ServerUnresponsiveDisconnects}");
        Console.WriteLine($"all_connected_ms={inSummary.AllConnectedMs:F3}");
        Console.WriteLine($"warmup_seconds={inSummary.WarmupSeconds}");
        Console.WriteLine($"measure_seconds={inSummary.MeasureSeconds}");
        Console.WriteLine($"timeout_ms={inSummary.TimeoutMs}");
        Console.WriteLine(
            $"retransmission_timeout_ms={inSummary.RetransmissionTimeoutMs}");
        Console.WriteLine(
            $"retransmission_max_count={inSummary.RetransmissionMaxCount}");
        Console.WriteLine($"pings_sent={inSummary.PingsSent}");
        Console.WriteLine($"pongs_received={inSummary.PongsReceived}");
        Console.WriteLine($"timeouts={inSummary.Timeouts}");
        Console.WriteLine($"timeout_rate_percent={inSummary.TimeoutRatePercent:F6}");
        Console.WriteLine($"invalid_responses={inSummary.InvalidResponses}");
        Console.WriteLine($"send_failures={inSummary.SendFailures}");
        Console.WriteLine(
            $"achieved_round_trips_per_second={inSummary.AchievedRoundTripsPerSecond:F3}");
        Console.WriteLine($"rtt_avg_ms={inSummary.RttAverageMs:F3}");
        Console.WriteLine($"rtt_min_ms={inSummary.RttMinMs:F3}");
        Console.WriteLine($"rtt_p50_ms={inSummary.RttP50Ms:F3}");
        Console.WriteLine($"rtt_p95_ms={inSummary.RttP95Ms:F3}");
        Console.WriteLine($"rtt_p99_ms={inSummary.RttP99Ms:F3}");
        Console.WriteLine($"rtt_p999_ms={inSummary.RttP999Ms:F3}");
        Console.WriteLine($"rtt_max_ms={inSummary.RttMaxMs:F3}");
        Console.WriteLine($"client_rps_min={inSummary.ClientRpsMin:F3}");
        Console.WriteLine($"client_rps_p50={inSummary.ClientRpsP50:F3}");
        Console.WriteLine($"client_rps_p95={inSummary.ClientRpsP95:F3}");
        Console.WriteLine($"client_rps_max={inSummary.ClientRpsMax:F3}");
        Console.WriteLine($"process_cpu_core_percent={inSummary.ProcessCpuCorePercent:F3}");
        Console.WriteLine($"process_cpu_host_percent={inSummary.ProcessCpuHostPercent:F3}");
        Console.WriteLine($"process_working_set_mb={inSummary.ProcessWorkingSetMb:F3}");
        Console.WriteLine($"process_peak_working_set_mb={inSummary.ProcessPeakWorkingSetMb:F3}");
        Console.WriteLine("=== END_BENCHMARK_RESULT ===");
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
