using System.Diagnostics;
using MultiSocketRUDPBotTester.Buffer;
using Serilog;

namespace MultiSocketRUDPBotTester.Contents.Client
{
    public sealed class RttTestRunner
    {
        private const int DetailedSampleCount = 5;
        private const int ReportInterval = 100000;
        private const double TailLatencyLogThresholdMs = 0.5;
        private readonly Client client;

        public RttTestRunner(Client inClient)
        {
            client = inClient;
        }

        public async Task<RttTestSummary> RunAsync(
            int inSampleCount,
            int inTimeoutMs,
            CancellationToken inCancellationToken)
        {
            if (inSampleCount <= 0)
            {
                throw new ArgumentOutOfRangeException(nameof(inSampleCount));
            }

            await client.WaitUntilConnectedAsync(inTimeoutMs, inCancellationToken).ConfigureAwait(false);

            double minRttMs = double.MaxValue;
            double maxRttMs = 0;
            double totalRttMs = 0;
            var stopwatch = Stopwatch.StartNew();

            for (var sampleIndex = 1; sampleIndex <= inSampleCount; ++sampleIndex)
            {
                inCancellationToken.ThrowIfCancellationRequested();

                var pongWaitTask = client.WaitForPongAsync(inTimeoutMs, inCancellationToken);
                var sentTimestamp = Stopwatch.GetTimestamp();
                client.BeginRttSample(sentTimestamp);
                await client.SendPingAsync().ConfigureAwait(false);

                var pong = await pongWaitTask.ConfigureAwait(false);
                if (pong == null)
                {
                    throw new TimeoutException($"Timed out waiting for Pong at sample {sampleIndex}.");
                }

                var rttMs = Stopwatch.GetElapsedTime(sentTimestamp).TotalMilliseconds;
                TryLogTailLatency(sampleIndex, sentTimestamp, rttMs);
                totalRttMs += rttMs;
                minRttMs = Math.Min(minRttMs, rttMs);
                maxRttMs = Math.Max(maxRttMs, rttMs);

                if (ShouldPrintProgress(sampleIndex) || sampleIndex == inSampleCount)
                {
                    var averageRttMs = totalRttMs / sampleIndex;
                    Log.Information(
                        "seq={Sequence} rtt={RttMs:F3} ms avg={AverageMs:F3} ms min={MinMs:F3} ms max={MaxMs:F3} ms samples={SampleCount}",
                        sampleIndex,
                        rttMs,
                        averageRttMs,
                        minRttMs,
                        maxRttMs,
                        sampleIndex);
                }
            }

            stopwatch.Stop();

            var summary = new RttTestSummary
            {
                SampleCount = inSampleCount,
                AverageRttMs = totalRttMs / inSampleCount,
                MinRttMs = minRttMs,
                MaxRttMs = maxRttMs,
                ElapsedSeconds = stopwatch.Elapsed.TotalSeconds
            };

            Log.Information(
                "summary samples={SampleCount} avg={AverageMs:F3} ms min={MinMs:F3} ms max={MaxMs:F3} ms elapsed={ElapsedSeconds:F3} s",
                summary.SampleCount,
                summary.AverageRttMs,
                summary.MinRttMs,
                summary.MaxRttMs,
                summary.ElapsedSeconds);

            return summary;
        }

        private static bool ShouldPrintProgress(int inSampleCount)
        {
            return inSampleCount <= DetailedSampleCount || (inSampleCount % ReportInterval) == 0;
        }

        private void TryLogTailLatency(int inSampleIndex, long inSentTimestamp, double inRttMs)
        {
            if (inRttMs < TailLatencyLogThresholdMs)
            {
                return;
            }

            var resumeTimestamp = Stopwatch.GetTimestamp();
            if (!client.TryCreateRttTraceSnapshot(resumeTimestamp, out var snapshot) ||
                !Client.ShouldLogTailLatency(snapshot))
            {
                return;
            }

            var socketToFastPathUs = Stopwatch.GetElapsedTime(
                snapshot.SocketReceiveTimestamp,
                snapshot.FastPathTimestamp).TotalMicroseconds;
            var fastPathToResumeUs = Stopwatch.GetElapsedTime(
                snapshot.FastPathTimestamp,
                snapshot.ResumeTimestamp).TotalMicroseconds;
            var socketToResumeUs = Stopwatch.GetElapsedTime(
                snapshot.SocketReceiveTimestamp,
                snapshot.ResumeTimestamp).TotalMicroseconds;
            var sendToSocketUs = Stopwatch.GetElapsedTime(
                snapshot.SendTimestamp,
                snapshot.SocketReceiveTimestamp).TotalMicroseconds;

            Log.Information(
                "[ClientTailLatency] seq={Sequence} sendToSocketUs={SendToSocketUs:F0} socketToFastPathUs={SocketToFastPathUs:F0} fastPathToResumeUs={FastPathToResumeUs:F0} socketToResumeUs={SocketToResumeUs:F0} totalRttUs={TotalRttUs:F0}",
                inSampleIndex,
                sendToSocketUs,
                socketToFastPathUs,
                fastPathToResumeUs,
                socketToResumeUs,
                Stopwatch.GetElapsedTime(inSentTimestamp, snapshot.ResumeTimestamp).TotalMicroseconds);
        }
    }
}
