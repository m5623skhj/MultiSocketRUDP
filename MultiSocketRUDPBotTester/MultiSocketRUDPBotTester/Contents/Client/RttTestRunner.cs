using System.Diagnostics;
using System.Net.NetworkInformation;
using MultiSocketRUDPBotTester.Buffer;
using Serilog;

namespace MultiSocketRUDPBotTester.Contents.Client
{
    public sealed class RttTestRunner
    {
        
        private const int DetailedSampleCount = 5;
        private const int ReportInterval = 100000;
        private const double TailLatencyLogThresholdMs = 0.5;
        private const double RetransmissionSuspectedThresholdMs = 40.0
        private readonly Client client;

        public RttTestRunner(Client inClient)
        {
            client = inClient;
        }

        public Task<RttTestSummary> RunAsync(
            int inSampleCount,
            int inTimeoutMs,
            CancellationToken inCancellationToken)
        {
            return RunAsync(inSampleCount, inTimeoutMs, 0.0, 0, inCancellationToken);
        }

        public async Task<RttTestSummary> RunAsync(
            int inSampleCount,
            int inTimeoutMs,
            double inLossRate,
            int inLossSeed,
            CancellationToken inCancellationToken)
        {
            if (inSampleCount <= 0)
            {
                throw new ArgumentOutOfRangeException(nameof(inSampleCount));
            }

            await client.WaitUntilConnectedAsync(inTimeoutMs, inCancellationToken).ConfigureAwait(false);

            var isLossSimulationEnabled = inLossRate > 0.0;
            if (isLossSimulationEnabled)
            {
                client.SetPacketLossSimulation(inLossRate, inLossSeed);
                Log.Infomation(
                    "Packet loss simulation enabled: lossRate={LossRate} seed={Seed}",
                        inLossRate,
                        inLossSeed);
            }
            
            var udpBefore = CaptureUdpStats();
            
            double minRttMs = double.MaxValue;
            double maxRttMs = 0;
            double totalRttMs = 0;
            var rttSamples = new List<double>(inSampleCount);
            var stopwatch = Stopwatch.StartNew();

            try
            {
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
                    if (!isLossSimulationEnabled)
                    {
                        TryLogTailLatency(sampleIndex, sentTimestamp, rttMs);
                    }
                    totalRttMs += rttMs;
                    minRttMs = Math.Min(minRttMs, rttMs);
                    maxRttMs = Math.Max(maxRttMs, rttMs);
                    rttSamples.Add(rttMs);

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
            }
            finally
            {
                if (isLossSimulationEnabled)
                {
                    client.SetPacketLossSimulation(0.0, 0);
                }
            }

            stopwatch.Stop();

            var udpAfter = CaptureUdpStats();
            Log.Information(
                "[UdpStats] recv={Recv} sent={Sent} discarded(noPort)={Disc} errors(inErrors)={Err}",
                udpAfter.received - udpBefore.received,
                udpAfter.sent - udpBefore.sent,
                udpAfter.discarded - udpBefore.discarded,
                udpAfter.errors - udpBefore.errors);
            
            rttSamples.Sort();
            var summary = new RttTestSummary
            {
                SampleCount = inSampleCount,
                AverageRttMs = totalRttMs / inSampleCount,
                MinRttMs = minRttMs,
                MaxRttMs = maxRttMs,
                P50RttMs = Percentile(rttSamples, 50.0),
                P95RttMs = Percentile(rttSamples, 95.0),
                P99RttMs = Percentile(rttSamples, 99.0),
                RetransmissionSuspectedCount = rttSamplesMs.Count(
                    rtt => rtt >= RetransmissionSuspectedThresholdMs),
                LossRate = inLossRate,
                LossSeed = inLossSeed,
                ElapsedSeconds = stopwatch.Elapsed.TotalSeconds
            };

            Log.Information(
                "summary samples={SampleCount} avg={AverageMs:F3} ms min={MinMs:F3} ms p50={P50Ms:F3} ms p95={P95Ms:F3} ms p99={P99Ms:F3} ms max={MaxMs:F3} ms retransmissionSuspected={RetransmissionSuspected} lossRate={LossRate} seed={Seed} elapsed={ElapsedSeconds:F3} s",
                summary.SampleCount,
                summary.AverageRttMs,
                summary.MinRttMs,
                summary.MaxRttMs,
                summary.P50RttMs,
                summary.P95RttMs,
                summary.P99RttMs,
                summary.ElapsedSeconds);

            return summary;
        }

        /// <summary>
        /// 현재 시스템의 IPv4 UDP 통계를 캡처합니다.
        /// 여기에는 수신, 송신, 폐기된(포트 없음), 오류가 있는 데이터그램 수가 포함됩니다.
        /// </summary>
        /// <returns>
        /// (수신된 데이터그램 수, 송신된 데이터그램 수, 폐기된 데이터그램 수, 오류가 있는 데이터그램 수) 튜플을 반환합니다.
        /// 통계 캡처 중 오류가 발생하면 모든 값이 0으로 설정된 튜플을 반환하고 경고를 로깅합니다.
        /// </returns>
        /// <remarks>
        /// 이 함수는 다음을 수행합니다:
        /// <list type="bullet">
        /// <item><see cref="IPGlobalProperties"/>를 사용하여 시스템의 UDP IPv4 통계를 가져옵니다. (상태 변화 없음)</item>
        /// <item>통계 정보를 튜플 형태로 반환하며, 이는 시스템의 현재 UDP 네트워크 활동 스냅샷을 제공합니다.</item>
        /// <item>통계를 가져오는 과정에서 예외가 발생하면, 이를 경고로 로깅하고 기본값 (0, 0, 0, 0)을 반환하여 프로그램의 강제 종료를 방지합니다. (Side effect: 로깅, 실패 조건)</item>
        /// </list>
        /// </remarks>
        private static (long received, long sent, long discarded, long errors) CaptureUdpStats()
        {
            try
            {
                var stats = IPGlobalProperties.GetIPGlobalProperties().GetUdpIPv4Statistics();
                return (stats.DatagramsReceived, stats.DatagramsSent, stats.IncomingDatagramsDiscarded, stats.IncomingDatagramsWithErrors);
            }
            catch (Exception ex)
            {
                Log.Warning("CaptureUdpStats failed: {Error}", ex.Message);
                return (0, 0, 0, 0);
            }
        }

        private static double Percentile(List<double> sortedSamples, double percentile)
        {
            if (sortedSamples.Count == 0)
            {
                return 0;
            }

            var rank = (int)Math.Ceiling(percentile / 100.0 * sortedSamples.Count);
            var index = Math.Clamp(rank - 1, 0, sortedSamples.Count - 1);
            return sortedSamples[index];
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
