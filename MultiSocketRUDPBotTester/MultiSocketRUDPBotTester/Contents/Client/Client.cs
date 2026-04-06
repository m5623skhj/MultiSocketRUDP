using MultiSocketRUDPBotTester.Bot;
using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.ClientCore;
using Serilog;
using System.Collections.Concurrent;

namespace MultiSocketRUDPBotTester.Contents.Client
{
    public partial class Client : RudpSession
    {
        private ActionGraph actionGraph = new();
        public RuntimeContext GlobalContext { get; }

        private readonly ConcurrentDictionary<PacketId, List<TaskCompletionSource<NetBuffer?>>> _packetWaiters = new();

        public Client(byte[] sessionInfoStream)
            : base(sessionInfoStream)
        {
            RegisterPacketHandlers();
            GlobalContext = new RuntimeContext(this, null);
        }

        public Task<NetBuffer?> WaitForNextPacketAsync(PacketId packetId, int timeoutMs, CancellationToken cancellationToken)
        {
            var tcs = new TaskCompletionSource<NetBuffer?>(TaskCreationOptions.RunContinuationsAsynchronously);

            var waiters = _packetWaiters.GetOrAdd(packetId, _ => new List<TaskCompletionSource<NetBuffer?>>());
            lock (waiters)
            {
                waiters.Add(tcs);
            }

            var linkedCts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            linkedCts.CancelAfter(timeoutMs);
            linkedCts.Token.Register(() =>
            {
                tcs.TrySetResult(null);
                linkedCts.Dispose();
            }, useSynchronizationContext: false);

            return tcs.Task;
        }

        public void SetActionGraph(ActionGraph graph)
        {
            actionGraph = graph;
        }

        protected override void OnConnected()
        {
            Log.Information("Client connected, triggering OnConnected event");
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

            var bufferKey = $"__received_{packetId}";

            GlobalContext.Set(bufferKey, buffer);
            GlobalContext.Set($"{bufferKey}_timestamp", CommonFunc.GetNowMs());

            if (packetHandlerDictionary.TryGetValue(packetId, out var action))
            {
                action.Execute(buffer);
            }

            if (_packetWaiters.TryGetValue(packetId, out var waiters))
            {
                List<TaskCompletionSource<NetBuffer?>> snapshot;
                lock (waiters)
                {
                    snapshot = waiters.ToList();
                    waiters.Clear();
                }

                foreach (var tcs in snapshot)
                {
                    tcs.TrySetResult(buffer);
                }
            }

            actionGraph.TriggerEvent(this, TriggerType.OnPacketReceived, packetId, buffer);
        }
    }
}
