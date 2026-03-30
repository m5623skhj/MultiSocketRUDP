using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.Contents.Client;
using Serilog;

namespace MultiSocketRUDPBotTester.Bot
{
    public class SendPacketNode : ActionNodeBase
    {
        public PacketId PacketId { get; set; }
        public Func<Client, NetBuffer>? PacketBuilder { get; set; }
        public Dictionary<string, object> FieldValues { get; set; } = new();

        public override void Execute(Client client, NetBuffer? receivedPacket = null)
        {
            try
            {
                var buffer = (PacketBuilder != null ?
                    PacketBuilder(client) : BuildFromSchema()) ?? throw new Exception($"Failed to build packet buffer for PacketId: ${PacketId}");

                _ = Task.Run(async () => await client.SendPacket(buffer, PacketId))
                    .ContinueWith(t => Log.Error(t.Exception!,
                        "SendPacketNode failed: {PacketId}", PacketId),
                        TaskContinuationOptions.OnlyOnFaulted);
                Log.Debug("Sent packet: {PacketId}", PacketId);
            }
            catch (Exception ex)
            {
                Log.Error("Failed to send packet: {Message}", ex.Message);
            }
        }

        private NetBuffer? BuildFromSchema()
        {
            var schema = PacketSchema.Get(PacketId);
            if (schema == null)
            {
                return null;
            }

            var buf = new NetBuffer();
            buf.ReserveHeader();
            foreach (var field in schema)
            {
                var value = FieldValues.TryGetValue(field.Name, out var v) ?
                    v : field.DefaultValue;
                switch (field.Type)
                {
                    case FieldType.Byte:
                        {
                            buf.WriteByte(Convert.ToByte(value));
                            break;
                        }
                    case FieldType.Ushort:
                        {
                            buf.WriteUShort(Convert.ToUInt16(value));
                            break;
                        }
                    case FieldType.Int:
                        {
                            buf.WriteInt(Convert.ToInt32(value));
                            break;
                        }
                    case FieldType.Uint:
                        {
                            buf.WriteUInt(Convert.ToUInt32(value));
                            break;
                        }
                    case FieldType.Ulong:
                        {
                            buf.WriteULong(Convert.ToUInt64(value));
                            break;
                        }
                    case FieldType.String:
                        {
                            buf.WriteString(Convert.ToString(value) ?? string.Empty);
                            break;
                        }
                }
            }

            return buf;
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

                var context = client.GlobalContext;
                context.SetPacket(receivedPacket);
                var visited = new HashSet<ActionNodeBase>();
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
            var delay = Random.Shared.Next(MinDelayMilliseconds, MaxDelayMilliseconds + 1);

            Task.Run(async () =>
            {
                await Task.Delay(delay);
                Log.Debug("RandomDelayNode: Delayed for {Delay}ms (range: {Min}-{Max}ms", delay, MinDelayMilliseconds, MaxDelayMilliseconds);

                var context = client.GlobalContext;
                context.SetPacket(receivedPacket);
                var visited = new HashSet<ActionNodeBase>();
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
                Log.Information("DisconnectNode: Disconnecting - Reason: {Reason}", Reason);
                client.Disconnect();
            }
            catch (Exception ex)
            {
                Log.Error("DisconnectNode failed: {Message}", ex.Message);
            }
        }
    }
}