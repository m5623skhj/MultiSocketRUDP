using MultiSocketRUDPBotTester.Bot;
using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.ClientCore;
using Serilog;
using System.Collections.Concurrent;
using System.Diagnostics;

namespace MultiSocketRUDPBotTester.Contents.Client
{
    public partial class Client : RudpSession
    {
        private ActionGraph actionGraph = new();
        public RuntimeContext GlobalContext { get; }

        private readonly ConcurrentDictionary<PacketId, ConcurrentDictionary<long, TaskCompletionSource<NetBuffer?>>> _packetWaiters = new();
        private long _nextWaiterId;
        private long _tracePingSentCount;
        private long _tracePongRecvCount;
        private long _traceAckRecvCount;
        private long _traceRetransmissionCount;
        private long _traceWaitRegisteredCount;
        private long _traceWaitTimeoutCount;
        private long _traceWaitCompletedCount;
        private long _traceLastPongRttMs;
        private long _traceMaxPongRttMs;
        private long _traceLastDeliveredPongSequence;
        private long _traceOldOrDuplicatePacketCount;
        private readonly ConcurrentQueue<(PacketSequence sequence, long tick)> _tracePendingPingTicks = new();

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
            Interlocked.Increment(ref _traceWaitRegisteredCount); // TEMP_TPS_TRACE
            
            try
            {
                var result = await tcs.Task.WaitAsync(TimeSpan.FromMilliseconds(timeoutMs), cancellationToken).ConfigureAwait(false);
                if (result != null)
                {
                    Interlocked.Increment(ref _traceWaitCompletedCount); // TEMP_TPS_TRACE
                }

                return result;
            }
            catch (TimeoutException)
            {
                Interlocked.Increment(ref _traceWaitTimeoutCount); // TEMP_TPS_TRACE
                Log.Warning(
                    "[{Tag}][BOT_WAIT_TIMEOUT] sessionId={SessionId} packetId={PacketId} pendingSend={PendingSend} waiterCount={WaiterCount} expectedRecvSequence={ExpectedRecvSequence} holdingCount={HoldingCount} holdingFirst={HoldingFirst} holdingLast={HoldingLast} lastDeliveredPongSequence={LastDeliveredPongSequence}",
                    TempTpsTraceTag,
                    GetSessionId(),
                    packetId,
                    GetPendingSendBufferCount(),
                    GetCurrentWaiterCount(),
                    GetExpectedRecvSequenceForTrace(),
                    GetHoldingPacketCountForTrace(),
                    GetHoldingFirstForTrace(),
                    GetHoldingLastForTrace(),
                    Interlocked.Read(ref _traceLastDeliveredPongSequence));
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

        protected override void OnConnected()
        {
            Log.Information("Client connected, triggering OnConnected event");
            _ = Task.Run(TraceSummaryLoopAsync, CancellationToken.Token); // TEMP_TPS_TRACE
            actionGraph.TriggerEvent(this, TriggerType.OnConnected);
        }

        protected override void OnDisconnected()
        {
            Log.Information("Client disconnected, triggering OnDisconnected event");
            actionGraph.TriggerEvent(this, TriggerType.OnDisconnected);

            GlobalContext.Clear();
        }

        protected override void OnRecvPacket(PacketId packetId, NetBuffer buffer)
        {
            Log.Debug("Received packet with ID: {PacketId}", packetId);

            if (packetId == PacketId.Pong) // TEMP_TPS_TRACE
            {
                Interlocked.Increment(ref _tracePongRecvCount);
                Interlocked.Exchange(ref _traceLastDeliveredPongSequence, GetExpectedRecvSequenceForTrace());
                if (_tracePendingPingTicks.TryDequeue(out var pingInfo))
                {
                    var elapsedMs = (long)Stopwatch.GetElapsedTime(pingInfo.tick).TotalMilliseconds;
                    Interlocked.Exchange(ref _traceLastPongRttMs, elapsedMs);
                    UpdateMaxRtt(elapsedMs);
                }
            }

            if (packetHandlerDictionary.TryGetValue(packetId, out var action))
            {
                action.Execute(buffer);
            }

            if (_packetWaiters.TryGetValue(packetId, out var waiters))
            {
                foreach (var kvp in waiters.ToArray())
                {
                    if (waiters.TryRemove(kvp.Key, out var tcs))
                    {
                        tcs.TrySetResult(buffer);
                    }
                }
            }

            actionGraph.TriggerEvent(this, TriggerType.OnPacketReceived, packetId, buffer);
        }

        protected override void OnTracePacketSent(PacketId packetId, PacketSequence packetSequence, PacketType packetType)
        {
            if (packetType != PacketType.SendType)
            {
                return;
            }

            if (packetId == PacketId.Ping)
            {
                Interlocked.Increment(ref _tracePingSentCount);
                _tracePendingPingTicks.Enqueue((packetSequence, Stopwatch.GetTimestamp()));
            }
        }

        protected override void OnTracePacketAcked(PacketSequence packetSequence)
        {
            Interlocked.Increment(ref _traceAckRecvCount);
        }

        protected override void OnTracePacketRetransmitted(PacketSequence packetSequence, long retransmissionCount, int pendingSendCount)
        {
            Interlocked.Increment(ref _traceRetransmissionCount);
            Log.Debug(
                "[{Tag}][BOT_RETRANSMIT] sessionId={SessionId} seq={Sequence} retransmissionCount={RetransmissionCount} pendingSend={PendingSend} expectedRecvSequence={ExpectedRecvSequence} holdingCount={HoldingCount} holdingFirst={HoldingFirst} holdingLast={HoldingLast} waiterCount={WaiterCount}",
                TempTpsTraceTag,
                GetSessionId(),
                packetSequence,
                retransmissionCount,
                pendingSendCount,
                GetExpectedRecvSequenceForTrace(),
                GetHoldingPacketCountForTrace(),
                GetHoldingFirstForTrace(),
                GetHoldingLastForTrace(),
                GetCurrentWaiterCount());
        }

        private async Task TraceSummaryLoopAsync()
        {
            try
            {
                using var timer = new PeriodicTimer(TimeSpan.FromSeconds(1));
                while (await timer.WaitForNextTickAsync(CancellationToken.Token).ConfigureAwait(false))
                {
                    Log.Information(
                        "[{Tag}][BOT_SUMMARY] sessionId={SessionId} pingSent={PingSent} pongRecv={PongRecv} ackRecv={AckRecv} retransmits={Retransmits} waitRegistered={WaitRegistered} waitCompleted={WaitCompleted} waitTimeout={WaitTimeout} pendingSend={PendingSend} waiterCount={WaiterCount} expectedRecvSequence={ExpectedRecvSequence} holdingCount={HoldingCount} holdingFirst={HoldingFirst} holdingLast={HoldingLast} oldOrDuplicatePackets={OldOrDuplicatePackets} lastDeliveredPongSequence={LastDeliveredPongSequence} lastPongRttMs={LastPongRttMs} maxPongRttMs={MaxPongRttMs}",
                        TempTpsTraceTag,
                        GetSessionId(),
                        Interlocked.Exchange(ref _tracePingSentCount, 0),
                        Interlocked.Exchange(ref _tracePongRecvCount, 0),
                        Interlocked.Exchange(ref _traceAckRecvCount, 0),
                        Interlocked.Exchange(ref _traceRetransmissionCount, 0),
                        Interlocked.Exchange(ref _traceWaitRegisteredCount, 0),
                        Interlocked.Exchange(ref _traceWaitCompletedCount, 0),
                        Interlocked.Exchange(ref _traceWaitTimeoutCount, 0),
                        GetPendingSendBufferCount(),
                        GetCurrentWaiterCount(),
                        GetExpectedRecvSequenceForTrace(),
                        GetHoldingPacketCountForTrace(),
                        GetHoldingFirstForTrace(),
                        GetHoldingLastForTrace(),
                        Interlocked.Exchange(ref _traceOldOrDuplicatePacketCount, 0),
                        Interlocked.Read(ref _traceLastDeliveredPongSequence),
                        Interlocked.Read(ref _traceLastPongRttMs),
                        Interlocked.Read(ref _traceMaxPongRttMs));
                }
            }
            catch (OperationCanceledException)
            {
            }
        }

        private int GetCurrentWaiterCount()
        {
            var waiterCount = 0;
            foreach (var waitersById in _packetWaiters.Values)
            {
                waiterCount += waitersById.Count;
            }

            return waiterCount;
        }

        private long GetHoldingFirstForTrace()
        {
            return TryGetHoldingPacketRangeForTrace(out var first, out _)
                ? (long)first
                : -1;
        }

        private long GetHoldingLastForTrace()
        {
            return TryGetHoldingPacketRangeForTrace(out _, out var last)
                ? (long)last
                : -1;
        }

        private void UpdateMaxRtt(long elapsedMs)
        {
            while (true)
            {
                var currentMax = Interlocked.Read(ref _traceMaxPongRttMs);
                if (elapsedMs <= currentMax)
                {
                    return;
                }

                if (Interlocked.CompareExchange(ref _traceMaxPongRttMs, elapsedMs, currentMax) == currentMax)
                {
                    return;
                }
            }
        }

        protected override void OnTraceOldOrDuplicatePacket(PacketId packetId, PacketSequence packetSequence, PacketSequence expectedPacketSequence)
        {
            Interlocked.Increment(ref _traceOldOrDuplicatePacketCount);
        }
    }
}
