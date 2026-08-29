using Serilog;
using MultiSocketRUDPBotTester.Bot;
using MultiSocketRUDPBotTester.Contents.Client;
using System.Diagnostics;

namespace MultiSocketRUDPBotTester.ClientCore
{
    public sealed class BotTesterCore
    {
        public static readonly BotTesterCore Instance = new();

        private readonly SessionGetter sessionGetter = new();
        private readonly Dictionary<SessionIdType, Client> sessionDictionary = new();
        private readonly Lock sessionDictionaryLock = new();
        private BotTestCompletionTracker? activeBotTestCompletion;

        private string hostIp = "";
        private ushort hostPort;

        private ActionGraph botActionGraph = new();
        private List<NodeVisual>? savedNodeVisuals;

        public event Action<BotTestResult>? BotTestCompleted;

        public void SetConnectionInfo(string targetIp, ushort targetPort)
        {
            if (string.IsNullOrWhiteSpace(targetIp))
            {
                throw new ArgumentException("IP cannot be empty", nameof(targetIp));
            }

            if (targetPort == 0)
            {
                throw new ArgumentException("Port cannot be 0", nameof(targetPort));
            }

            hostIp = targetIp;
            hostPort = targetPort;
        }

        public void SetBotActionGraph(ActionGraph graph)
        {
            botActionGraph = graph;
        }

        public void SaveGraphVisuals(List<NodeVisual> nodeVisuals)
        {
            savedNodeVisuals = nodeVisuals;
        }

        public void ClearSavedGraphVisuals()
        {
            savedNodeVisuals = null;
        }

        public List<NodeVisual>? GetSavedGraphVisuals()
        {
            return savedNodeVisuals;
        }

        public async Task StartBotTest(ushort numOfBot)
        {
            if (string.IsNullOrEmpty(hostIp) || hostPort == 0)
            {
                throw new InvalidOperationException("Connection info not set. Call SetConnectionInfo first.");
            }

            var rttSampleCollector = new BotRttSampleCollector();
            var completionTracker = new BotTestCompletionTracker(
                numOfBot,
                completedBotCount => NotifyBotTestCompleted(new BotTestResult
                {
                    BotCount = completedBotCount,
                    RttSummary = rttSampleCollector.CreateSummary()
                }));
            Interlocked.Exchange(ref activeBotTestCompletion, completionTracker)?.Cancel();

            for (var i = 0; i < numOfBot; ++i)
            {
                var rudpSession = await GetSessionInfoFromSessionBroker();
                if (rudpSession == null)
                {
                    completionTracker.Cancel();
                    Log.Error("StartBotTest() failed to get session info from session broker");
                    return;
                }

                rudpSession.EnableBotRttTracking(rttSampleCollector);
                rudpSession.SetActionGraph(botActionGraph);
                rudpSession.OnSessionDisconnected = (sessionId) =>
                {
                    lock (sessionDictionaryLock)
                    {
                        sessionDictionary.Remove(sessionId);
                        Log.Information("Session {Id} removed from dictionary after disconnect", sessionId);
                    }

                    completionTracker.MarkDisconnected();
                };

                lock (sessionDictionaryLock)
                {
                    sessionDictionary.Add(rudpSession.GetSessionId(), rudpSession);
                }

                sessionGetter.Close();
            }

            completionTracker.MarkSetupComplete();
        }

        public Task<RttTestSummary> StartRttTest(int inSampleCount, int inTimeoutMs)
        {
            return StartRttTest(inSampleCount, inTimeoutMs, 0.0, 0);
        }

        public async Task<RttTestSummary> StartRttTest(int inSampleCount, int inTimeoutMs, double inLossRate, int inLossSeed)
        {
            if (string.IsNullOrEmpty(hostIp) || hostPort == 0)
            {
                throw new InvalidOperationException("Connection info not set. Call SetConnectionInfo first.");
            }

            Client? rudpSession = null;
            try
            {
                rudpSession = await GetSessionInfoFromSessionBroker();
                if (rudpSession == null)
                {
                    throw new InvalidOperationException("Failed to get session info from session broker.");
                }

                rudpSession.EnableRttMode();
                rudpSession.OnSessionDisconnected = sessionId =>
                {
                    lock (sessionDictionaryLock)
                    {
                        sessionDictionary.Remove(sessionId);
                        Log.Information("Session {Id} removed from dictionary after disconnect", sessionId);
                    }
                };

                lock (sessionDictionaryLock)
                {
                    sessionDictionary[rudpSession.GetSessionId()] = rudpSession;
                }

                var runner = new RttTestRunner(rudpSession);
                return await runner.RunAsync(inSampleCount, inTimeoutMs, inLossRate, inLossSeed, rudpSession.CancellationToken.Token);
            }
            finally
            {
                sessionGetter.Close();
                rudpSession?.Disconnect();
            }
        }

        /// <summary>
        /// 모든 봇을 먼저 연결한 뒤 클라이언트별로 하나의 Ping만 outstanding 상태로
        /// 유지하는 폐루프 RTT 처리량 테스트를 실행합니다.
        /// </summary>
        public async Task<RttStressTestSummary> StartRttStressTest(
            RttStressTestConfiguration inConfiguration,
            CancellationToken inCancellationToken = default)
        {
            ArgumentNullException.ThrowIfNull(inConfiguration);
            if (string.IsNullOrEmpty(hostIp) || hostPort == 0)
            {
                throw new InvalidOperationException(
                    "Connection info not set. Call SetConnectionInfo first.");
            }
            if (inConfiguration.ClientCount <= 0 ||
                inConfiguration.ClientCount > ushort.MaxValue)
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

            const int connectionWaitTimeoutMs = 120000;
            var clients = new List<Client>(inConfiguration.ClientCount);
            var connectStartedTimestamp = Stopwatch.GetTimestamp();

            try
            {
                for (var index = 0; index < inConfiguration.ClientCount; ++index)
                {
                    inCancellationToken.ThrowIfCancellationRequested();
                    Client? session;
                    try
                    {
                        session = await GetSessionInfoFromSessionBroker().ConfigureAwait(false);
                    }
                    finally
                    {
                        sessionGetter.Close();
                    }

                    if (session == null)
                    {
                        throw new InvalidOperationException(
                            $"Failed to get session info for client {index + 1}.");
                    }

                    session.EnableRttMode();
                    session.ConfigureRetransmission(
                        inConfiguration.RetransmissionTimeoutMs,
                        inConfiguration.RetransmissionMaxCount);
                    session.OnSessionDisconnected = sessionId =>
                    {
                        lock (sessionDictionaryLock)
                        {
                            sessionDictionary.Remove(sessionId);
                        }
                    };

                    lock (sessionDictionaryLock)
                    {
                        sessionDictionary[session.GetSessionId()] = session;
                    }
                    clients.Add(session);
                }

                var connectionTasks = clients.Select(client =>
                    client.WaitUntilConnectedAsync(
                        connectionWaitTimeoutMs,
                        inCancellationToken));
                await Task.WhenAll(connectionTasks).ConfigureAwait(false);

                var allConnectedMs = Stopwatch.GetElapsedTime(
                    connectStartedTimestamp).TotalMilliseconds;
                var runner = new RttStressTestRunner();
                return await runner.RunAsync(
                    clients,
                    inConfiguration,
                    allConnectedMs,
                    inCancellationToken).ConfigureAwait(false);
            }
            finally
            {
                sessionGetter.Close();
                foreach (var client in clients)
                {
                    client.Disconnect();
                }
            }
        }

        /// <summary>
        /// Stops the currently active bot test.
        /// This cancels the associated completion tracker and forces all active bot sessions to disconnect.
        /// All sessions are removed from the internal dictionary.
        /// </summary>
        public void StopBotTest()
        {
            Interlocked.Exchange(ref activeBotTestCompletion, null)?.Cancel();

            lock (sessionDictionaryLock)
            {
                foreach (var session in sessionDictionary.Values)
                {
                    session.Disconnect();
                }
                sessionDictionary.Clear();
            }
        }

        private void NotifyBotTestCompleted(BotTestResult inResult)
        {
            try
            {
                BotTestCompleted?.Invoke(inResult);
            }
            catch (Exception ex)
            {
                Log.Error(ex, "Bot test completion notification failed");
            }
        }

        public int GetActiveBotCount()
        {
            lock (sessionDictionaryLock)
            {
                return sessionDictionary.Values.Count(client => client.IsConnected());
            }
        }

        private async Task<Client?> GetSessionInfoFromSessionBroker()
        {
            var buffer = new byte[1024];
            var totalBytes = 0;
            var payloadLength = 0;
            try
            {
                await sessionGetter.ConnectAsync(hostIp, hostPort);
                while (true)
                {
                    var receivedBytes = await sessionGetter.ReceiveAsync(buffer, totalBytes);
                    if (receivedBytes == 0)
                    {
                        break;
                    }

                    totalBytes += receivedBytes;
                    if (totalBytes < GlobalConstants.PacketHeaderSize)
                    {
                        continue;
                    }

                    if (payloadLength == 0)
                    {
                        payloadLength = BitConverter.ToUInt16(buffer, GlobalConstants.PayloadPosition);
                    }

                    if (totalBytes >= payloadLength + GlobalConstants.PacketHeaderSize)
                    {
                        break;
                    }
                }
            }
            catch (Exception e)
            {
                Log.Error(e, "Failed to get session info from session broker");
                throw new InvalidOperationException("Failed to get session info from session broker. Check connection settings.", e);
            }

            Log.Debug("Received {N} bytes: {Hex}",
                totalBytes,
                BitConverter.ToString(buffer, 0, totalBytes));

            return new Client(buffer[GlobalConstants.PacketHeaderSize..totalBytes]);
        }
    }
}
