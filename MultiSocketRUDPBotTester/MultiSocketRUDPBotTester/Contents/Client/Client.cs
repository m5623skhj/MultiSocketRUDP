using MultiSocketRUDPBotTester.Bot;
using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.ClientCore;
using Serilog;

namespace MultiSocketRUDPBotTester.Contents.Client
{
    public partial class Client : RUDPSession
    {
        private ActionGraph actionGraph = new();
        public RuntimeContext GlobalContext { get; }

        public Client(byte[] sessionInfoStream)
            : base(sessionInfoStream)
        {
            RegisterPacketHandlers();
            GlobalContext = new RuntimeContext(this, null);
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

            actionGraph.TriggerEvent(this, TriggerType.OnPacketReceived, packetId, buffer);
        }
    }
}