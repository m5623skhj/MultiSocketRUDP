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
            Task.Run(async () =>
            {
                await Task.Delay(DelayMilliseconds);
                Log.Debug("Delayed for {Ms}ms", DelayMilliseconds);

                foreach (var nextNode in NextNodes)
                {
                    nextNode.Execute(client, receivedPacket);
                }
            });
        }
    }

    public class RandomDelayNode : ActionNodeBase
    {
        public int MinDelayMilliseconds { get; set; } = 500;
        public int MaxDelayMilliseconds { get; set; } = 2000;
        private static readonly Random Random = new();

        public override void Execute(Client client, NetBuffer? receivedPacket = null)
        {
            var delay = Random.Next(MinDelayMilliseconds, MaxDelayMilliseconds + 1);

            Task.Run(async () =>
            {
                await Task.Delay(delay);
                Log.Debug($"RandomDelayNode: Delayed for {delay}ms (range: {MinDelayMilliseconds}-{MaxDelayMilliseconds}ms)");

                foreach (var nextNode in NextNodes)
                {
                    nextNode.Execute(client, receivedPacket);
                }
            });
        }
    }

    public class LogNode : ActionNodeBase
    {
        public Func<Client, NetBuffer?, string>? MessageBuilder { get; set; }

        public override void Execute(Client client, NetBuffer? receivedPacket = null)
        {
            try
            {
                if (MessageBuilder == null)
                {
                    Log.Warning("LogNode has no message builder configured");
                    return;
                }

                var message = MessageBuilder(client, receivedPacket);

                Log.Information("╔══════════════════════════════════════════════════════════════");
                Log.Information("║ [Bot Log] {Message}", message);
                Log.Information("╚══════════════════════════════════════════════════════════════");
            }
            catch (Exception ex)
            {
                Log.Error("LogNode execution failed: {Message}", ex.Message);
            }
        }
    }

    public class DisconnectNode : ActionNodeBase
    {
        public string Reason { get; set; } = "User requested disconnect";

        public override void Execute(Client client, NetBuffer? receivedPacket = null)
        {
            try
            {
                Log.Information($"DisconnectNode: Disconnecting - Reason: {Reason}");
                client.Disconnect();
            }
            catch (Exception ex)
            {
                Log.Error($"DisconnectNode failed: {ex.Message}");
            }
        }
    }
}