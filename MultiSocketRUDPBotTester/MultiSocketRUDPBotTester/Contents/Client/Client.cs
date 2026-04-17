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

        private readonly ConcurrentDictionary<PacketId, ConcurrentDictionary<long, TaskCompletionSource<NetBuffer?>>> _packetWaiters = new();
        private long _nextWaiterId;

        public Client(byte[] sessionInfoStream)
            : base(sessionInfoStream)
        {
            RegisterPacketHandlers();
            GlobalContext = new RuntimeContext(this, null);
        }

        public Task<NetBuffer?> WaitForNextPacketAsync(PacketId packetId, int timeoutMs, CancellationToken cancellationToken)
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
    }
}
