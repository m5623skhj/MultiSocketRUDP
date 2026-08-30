using System.Diagnostics;
using MultiSocketRUDPBotTester.ClientCore;

namespace MultiSocketRUDPBotTester.Contents.Client
{
    public sealed class RttStressTestRunner
    {
        private sealed class ClientResult
        {
            public long PingsSent;
            public long PongsReceived;
            public long Timeouts;
            public long InvalidResponses;
            public long SendFailures;
            public int RuntimeDisconnected;
            public SessionDisconnectReason DisconnectReason;
        }

        private readonly record struct ProcessSample(
            TimeSpan TotalProcessorTime,
            long WorkingSetBytes,
            long PeakWorkingSetBytes,
            long CapturedTimestamp);

        public async Task<RttStressTestSummary> RunAsync(
            IReadOnlyList<Client> inClients,
            RttStressTestConfiguration inConfiguration,
            double inAllConnectedMs,
            CancellationToken inCancellationToken = default)
        {
            ArgumentNullException.ThrowIfNull(inClients);
            ArgumentNullException.ThrowIfNull(inConfiguration);
            ValidateConfiguration(inClients, inConfiguration);

            var histogram = new RttStressHistogram(inConfiguration.TimeoutMs);
            var clientResults = Enumerable
                .Range(0, inClients.Count)
                .Select(_ => new ClientResult())
                .ToArray();

            var warmupStartedTimestamp = Stopwatch.GetTimestamp();
            var measurementStartedTimestamp = warmupStartedTimestamp +
                SecondsToStopwatchTicks(inConfiguration.WarmupSeconds);
            var measurementEndedTimestamp = measurementStartedTimestamp +
                SecondsToStopwatchTicks(inConfiguration.MeasureSeconds);

            var workerTasks = new Task[inClients.Count];
            for (var index = 0; index < inClients.Count; ++index)
            {
                workerTasks[index] = RunClientAsync(
                    inClients[index],
                    clientResults[index],
                    histogram,
                    measurementStartedTimestamp,
                    measurementEndedTimestamp,
                    inConfiguration.TimeoutMs,
                    inCancellationToken);
            }

            await DelayUntilAsync(measurementStartedTimestamp, inCancellationToken)
                .ConfigureAwait(false);
            var processStart = CaptureProcessSample();

            await DelayUntilAsync(measurementEndedTimestamp, inCancellationToken)
                .ConfigureAwait(false);
            var processEnd = CaptureProcessSample();

            await Task.WhenAll(workerTasks).ConfigureAwait(false);
            return CreateSummary(
                inConfiguration,
                inAllConnectedMs,
                clientResults,
                histogram.CreateSnapshot(),
                processStart,
                processEnd);
        }

        private static async Task RunClientAsync(
            Client inClient,
            ClientResult inResult,
            RttStressHistogram inHistogram,
            long inMeasurementStartedTimestamp,
            long inMeasurementEndedTimestamp,
            int inTimeoutMs,
            CancellationToken inCancellationToken)
        {
            var sessionCancellationToken = inClient.CancellationToken.Token;
            using var linkedCancellation = CancellationTokenSource.CreateLinkedTokenSource(
                inCancellationToken,
                sessionCancellationToken);

            while (!linkedCancellation.IsCancellationRequested)
            {
                var sentTimestamp = Stopwatch.GetTimestamp();
                if (sentTimestamp >= inMeasurementEndedTimestamp)
                {
                    break;
                }

                var measured = sentTimestamp >= inMeasurementStartedTimestamp;
                var pongWaitTask = inClient.WaitForRttStressResponseAsync(
                    inTimeoutMs,
                    linkedCancellation.Token);

                try
                {
                    await inClient.SendRttStressRequestAsync().ConfigureAwait(false);
                    if (measured)
                    {
                        ++inResult.PingsSent;
                    }
                }
                catch (Exception) when (linkedCancellation.IsCancellationRequested)
                {
                    break;
                }
                catch (Exception) when (!linkedCancellation.IsCancellationRequested)
                {
                    ++inResult.SendFailures;
                    break;
                }

                try
                {
                    var pong = await pongWaitTask.ConfigureAwait(false);
                    if (!measured)
                    {
                        continue;
                    }

                    if (pong == null)
                    {
                        ++inResult.Timeouts;
                        continue;
                    }

                    if (!Client.IsValidRttStressResponse(pong))
                    {
                        ++inResult.InvalidResponses;
                        continue;
                    }

                    var completedTimestamp = Stopwatch.GetTimestamp();
                    ++inResult.PongsReceived;
                    inHistogram.Record(completedTimestamp - sentTimestamp);
                }
                catch (OperationCanceledException) when (linkedCancellation.IsCancellationRequested)
                {
                    break;
                }
            }

            if (!inCancellationToken.IsCancellationRequested &&
                sessionCancellationToken.IsCancellationRequested)
            {
                inResult.RuntimeDisconnected = 1;
                inResult.DisconnectReason = inClient.GetDisconnectReason();
            }
        }

        private static RttStressTestSummary CreateSummary(
            RttStressTestConfiguration inConfiguration,
            double inAllConnectedMs,
            IReadOnlyList<ClientResult> inClientResults,
            RttStressHistogramSnapshot inHistogram,
            ProcessSample inProcessStart,
            ProcessSample inProcessEnd)
        {
            var pingsSent = inClientResults.Sum(result => result.PingsSent);
            var pongsReceived = inClientResults.Sum(result => result.PongsReceived);
            var timeouts = inClientResults.Sum(result => result.Timeouts);
            var invalidResponses = inClientResults.Sum(result => result.InvalidResponses);
            var sendFailures = inClientResults.Sum(result => result.SendFailures);
            var runtimeDisconnects = inClientResults.Sum(result => result.RuntimeDisconnected);
            var retransmissionLimitDisconnects = inClientResults.Count(result =>
                result.DisconnectReason == SessionDisconnectReason.RetransmissionLimit);
            var serverUnresponsiveDisconnects = inClientResults.Count(result =>
                result.DisconnectReason == SessionDisconnectReason.ServerUnresponsive);
            var clientRates = inClientResults
                .Select(result => result.PongsReceived / (double)inConfiguration.MeasureSeconds)
                .Order()
                .ToArray();

            var processElapsedSeconds = Stopwatch.GetElapsedTime(
                inProcessStart.CapturedTimestamp,
                inProcessEnd.CapturedTimestamp).TotalSeconds;
            var processCpuSeconds = (
                inProcessEnd.TotalProcessorTime - inProcessStart.TotalProcessorTime).TotalSeconds;
            var processCpuCorePercent = processElapsedSeconds <= 0.0
                ? 0.0
                : processCpuSeconds / processElapsedSeconds * 100.0;
            var timeoutRatePercent = pingsSent == 0
                ? 0.0
                : timeouts * 100.0 / pingsSent;

            return new RttStressTestSummary
            {
                TargetClients = inConfiguration.ClientCount,
                ConnectedClients = inClientResults.Count,
                RuntimeDisconnects = runtimeDisconnects,
                RetransmissionLimitDisconnects = retransmissionLimitDisconnects,
                ServerUnresponsiveDisconnects = serverUnresponsiveDisconnects,
                AllConnectedMs = inAllConnectedMs,
                WarmupSeconds = inConfiguration.WarmupSeconds,
                MeasureSeconds = inConfiguration.MeasureSeconds,
                TimeoutMs = inConfiguration.TimeoutMs,
                RetransmissionTimeoutMs = inConfiguration.RetransmissionTimeoutMs,
                RetransmissionMaxCount = inConfiguration.RetransmissionMaxCount,
                ApplicationPayloadBytes = Client.RttStressApplicationPayloadBytes,
                PingsSent = pingsSent,
                PongsReceived = pongsReceived,
                Timeouts = timeouts,
                TimeoutRatePercent = timeoutRatePercent,
                InvalidResponses = invalidResponses,
                SendFailures = sendFailures,
                AchievedRoundTripsPerSecond = pongsReceived /
                    (double)inConfiguration.MeasureSeconds,
                RttAverageMs = inHistogram.AverageMs,
                RttMinMs = inHistogram.MinMs,
                RttP50Ms = inHistogram.P50Ms,
                RttP95Ms = inHistogram.P95Ms,
                RttP99Ms = inHistogram.P99Ms,
                RttP999Ms = inHistogram.P999Ms,
                RttMaxMs = inHistogram.MaxMs,
                ClientRpsMin = Percentile(clientRates, 0.0),
                ClientRpsP50 = Percentile(clientRates, 50.0),
                ClientRpsP95 = Percentile(clientRates, 95.0),
                ClientRpsMax = Percentile(clientRates, 100.0),
                ProcessCpuCorePercent = processCpuCorePercent,
                ProcessCpuHostPercent = processCpuCorePercent / Environment.ProcessorCount,
                ProcessWorkingSetMb = inProcessEnd.WorkingSetBytes / 1024.0 / 1024.0,
                ProcessPeakWorkingSetMb = Math.Max(
                    inProcessStart.PeakWorkingSetBytes,
                    inProcessEnd.PeakWorkingSetBytes) / 1024.0 / 1024.0
            };
        }

        private static void ValidateConfiguration(
            IReadOnlyList<Client> inClients,
            RttStressTestConfiguration inConfiguration)
        {
            if (inConfiguration.ClientCount <= 0 ||
                inConfiguration.ClientCount != inClients.Count)
            {
                throw new ArgumentOutOfRangeException(nameof(inConfiguration.ClientCount));
            }
            if (inConfiguration.WarmupSeconds < 0)
            {
                throw new ArgumentOutOfRangeException(nameof(inConfiguration.WarmupSeconds));
            }
            if (inConfiguration.MeasureSeconds <= 0)
            {
                throw new ArgumentOutOfRangeException(nameof(inConfiguration.MeasureSeconds));
            }
            if (inConfiguration.TimeoutMs <= 0 || inConfiguration.TimeoutMs > 60000)
            {
                throw new ArgumentOutOfRangeException(nameof(inConfiguration.TimeoutMs));
            }
        }

        private static long SecondsToStopwatchTicks(int inSeconds)
        {
            return checked((long)inSeconds * Stopwatch.Frequency);
        }

        private static async Task DelayUntilAsync(
            long inTargetTimestamp,
            CancellationToken inCancellationToken)
        {
            while (true)
            {
                inCancellationToken.ThrowIfCancellationRequested();
                var remaining = Stopwatch.GetElapsedTime(
                    Stopwatch.GetTimestamp(),
                    inTargetTimestamp);
                if (remaining <= TimeSpan.Zero)
                {
                    return;
                }

                if (remaining > TimeSpan.FromMilliseconds(2))
                {
                    await Task.Delay(remaining - TimeSpan.FromMilliseconds(1), inCancellationToken)
                        .ConfigureAwait(false);
                }
                else
                {
                    await Task.Yield();
                }
            }
        }

        private static ProcessSample CaptureProcessSample()
        {
            using var process = Process.GetCurrentProcess();
            process.Refresh();
            return new ProcessSample(
                process.TotalProcessorTime,
                process.WorkingSet64,
                process.PeakWorkingSet64,
                Stopwatch.GetTimestamp());
        }

        private static double Percentile(IReadOnlyList<double> inSortedValues, double inPercentile)
        {
            if (inSortedValues.Count == 0)
            {
                return 0.0;
            }

            var rank = Math.Max(1, (int)Math.Ceiling(
                inPercentile / 100.0 * inSortedValues.Count));
            return inSortedValues[Math.Clamp(rank - 1, 0, inSortedValues.Count - 1)];
        }
    }
}
