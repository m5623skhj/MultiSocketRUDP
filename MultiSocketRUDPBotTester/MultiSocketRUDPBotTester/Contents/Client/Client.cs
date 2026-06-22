using MultiSocketRUDPBotTester.Bot;
using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.ClientCore;
using Serilog;
using System.Collections.Concurrent;
using System.Diagnostics;

namespace MultiSocketRUDPBotTester.Contents.Client
{
    public readonly record struct RttTraceSnapshot(
        long SendTimestamp,
        long SocketReceiveTimestamp,
        long FastPathTimestamp,
        long ResumeTimestamp);

    public partial class Client : RudpSession
    {
        private const double TailLatencyLogThresholdMs = 1.0;
        private ActionGraph actionGraph = new();
        private volatile bool isRttModeEnabled;
        public RuntimeContext GlobalContext { get; }

        private readonly ConcurrentDictionary<PacketId, ConcurrentDictionary<long, TaskCompletionSource<NetBuffer?>>> _packetWaiters = new();
        private long _nextWaiterId;
        private long rttSendTimestamp;
        private long rttSocketReceiveTimestamp;
        private long rttFastPathTimestamp;

        public Client(byte[] sessionInfoStream)
            : base(sessionInfoStream)
        {
            RegisterPacketHandlers();
            GlobalContext = new RuntimeContext(this, null);
        }

        public async Task<NetBuffer?> WaitForNextPacketAsync(PacketId packetId, int timeoutMs, CancellationToken cancellationToken)
        {
            var tcs = new TaskCompletionSource<NetBuffer?>(TaskCreationOptions.RunContinuationsAsynchronously);
            var waiterId = Interlocked.Increment(ref _nextWaiterId);

            var waiters = _packetWaiters.GetOrAdd(packetId, _ => new ConcurrentDictionary<long, TaskCompletionSource<NetBuffer?>>());
            waiters[waiterId] = tcs;
            
            try
            {
                return await tcs.Task.WaitAsync(TimeSpan.FromMilliseconds(timeoutMs), cancellationToken).ConfigureAwait(false);
            }
            catch (TimeoutException)
            {
                return null;
            }
            finally
            {
                waiters.TryRemove(waiterId, out _);
            }
        }

        public void SetActionGraph(ActionGraph graph)
        {
            actionGraph = graph;
        }

        public void EnableRttMode()
        {
            isRttModeEnabled = true;
        }

        public async Task WaitUntilConnectedAsync(int timeoutMs, CancellationToken cancellationToken)
        {
            using var linkedCancellationTokenSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, CancellationToken.Token);
            var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
            while (!IsConnected())
            {
                linkedCancellationTokenSource.Token.ThrowIfCancellationRequested();
                if (DateTime.UtcNow >= deadline)
                {
                    throw new TimeoutException("Timed out waiting for the client session to connect.");
                }

                await Task.Delay(1, linkedCancellationTokenSource.Token).ConfigureAwait(false);
            }
        }

        public Task SendPingAsync()
        {
            var buffer = new NetBuffer(64);
            buffer.ReserveHeader();
            return SendPacket(buffer, PacketId.Ping);
        }

        public void BeginRttSample(long inSendTimestamp)
        {
            Interlocked.Exchange(ref rttSocketReceiveTimestamp, 0);
            Interlocked.Exchange(ref rttFastPathTimestamp, 0);
            Interlocked.Exchange(ref rttSendTimestamp, inSendTimestamp);
        }

        public Task<NetBuffer?> WaitForPongAsync(int timeoutMs, CancellationToken cancellationToken)
        {
            return WaitForNextPacketAsync(PacketId.Pong, timeoutMs, cancellationToken);
        }

        public bool TryCreateRttTraceSnapshot(long inResumeTimestamp, out RttTraceSnapshot outSnapshot)
        {
            var sendTimestamp = Interlocked.Read(ref rttSendTimestamp);
            var socketReceiveTimestamp = Interlocked.Read(ref rttSocketReceiveTimestamp);
            var fastPathTimestamp = Interlocked.Read(ref rttFastPathTimestamp);
            if (sendTimestamp == 0 || socketReceiveTimestamp == 0 || fastPathTimestamp == 0)
            {
                outSnapshot = default;
                return false;
            }

            outSnapshot = new RttTraceSnapshot(
                sendTimestamp,
                socketReceiveTimestamp,
                fastPathTimestamp,
                inResumeTimestamp);
            return true;
        }

        public static bool ShouldLogTailLatency(RttTraceSnapshot inSnapshot)
        {
            return Stopwatch.GetElapsedTime(inSnapshot.SendTimestamp, inSnapshot.ResumeTimestamp).TotalMilliseconds >= TailLatencyLogThresholdMs;
        }

        protected override void OnConnected()
        {
            Log.Information("Client connected, triggering OnConnected event");
            if (!isRttModeEnabled)
            {
                actionGraph.TriggerEvent(this, TriggerType.OnConnected);
            }
        }

        protected override void OnDisconnected()
        {
            Log.Information("Client disconnected, triggering OnDisconnected event");
            if (!isRttModeEnabled)
            {
                actionGraph.TriggerEvent(this, TriggerType.OnDisconnected);
            }

            GlobalContext.Clear();
        }

        protected override void OnRecvPacket(PacketId packetId, NetBuffer buffer)
        {
            Log.Debug("Received packet with ID: {PacketId}", packetId);

            if (packetHandlerDictionary.TryGetValue(packetId, out var action))
            {
                action.Execute(buffer);
            }

            CompletePacketWaiters(packetId, buffer);

            if (!isRttModeEnabled)
            {
                actionGraph.TriggerEvent(this, TriggerType.OnPacketReceived, packetId, buffer);
            }
        }

        protected override bool TryHandleRecvFastPath(PacketId packetId, NetBuffer buffer)
        {
            if (!isRttModeEnabled || packetId != PacketId.Pong)
            {
                return false;
            }

            Interlocked.Exchange(ref rttFastPathTimestamp, Stopwatch.GetTimestamp());
            CompletePacketWaiters(packetId, buffer);
            return true;
        }

        protected override void OnRttPacketReceived(PacketId packetId, long inReceiveTimestamp)
        {
            if (!isRttModeEnabled || packetId != PacketId.Pong)
            {
                return;
            }

            Interlocked.Exchange(ref rttSocketReceiveTimestamp, inReceiveTimestamp);
        }

        private void CompletePacketWaiters(PacketId packetId, NetBuffer buffer)
        {
            if (_packetWaiters.TryGetValue(packetId, out var waiters))
            {
                foreach (var kvp in waiters)
                {
                    if (waiters.TryRemove(kvp.Key, out var tcs))
                    {
                        tcs.TrySetResult(buffer);
                    }
                }
            }
        }
    }
}
