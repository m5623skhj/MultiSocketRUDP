using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.Contents.Client;
using Serilog;

namespace MultiSocketRUDPBotTester.Bot
{
    public class SendPacketNode : ActionNodeBase
    {
        public PacketId PacketId { get; set; }
        public Func<Client, NetBuffer>? PacketBuilder { get; set; }

        public override void Execute(Client client, NetBuffer? receivedPacket = null)
        {
            if (PacketBuilder == null)
            {
                Log.Warning("SendPacketNodeBase has no packet builder");
                return;
            }

            try
            {
                var buffer = PacketBuilder(client);
                _ = Task.Run(async () => await client.SendPacket(buffer, PacketId));
                Log.Debug("Sent packet: {PacketId}", PacketId);
            }
            catch (Exception ex)
            {
                Log.Error("Failed to send packet: {Message}", ex.Message);
            }
        }
    }

    public class CustomActionNode : ActionNodeBase
    {
        public Action<Client, NetBuffer?>? ActionHandler { get; set; }

        public override void Execute(Client client, NetBuffer? receivedPacket = null)
        {
            try
            {
                ActionHandler?.Invoke(client, receivedPacket);
                Log.Debug("Executed custom action: {Name}", Name);
            }
            catch (Exception ex)
            {
                Log.Error("Custom action failed: {Message}", ex.Message);
            }
        }
    }

    public class DelayNode : ActionNodeBase
    {
        public int DelayMilliseconds { get; set; }

        public override void Execute(Client client, NetBuffer? receivedPacket = null)
        {
            Task.Delay(DelayMilliseconds).Wait();
            Log.Debug("Delayed for {Ms}ms", DelayMilliseconds);
        }
    }

    public class LogNode : ActionNodeBase
    {
        public Func<Client, NetBuffer?, string>? MessageBuilder { get; set; }

        public override void Execute(Client client, NetBuffer? receivedPacket = null)
        {
            if (MessageBuilder == null)
            {
                return;
            }

            var message = MessageBuilder(client, receivedPacket);
            Log.Information("[Bot Action] {Message}", message);
        }
    }
}
