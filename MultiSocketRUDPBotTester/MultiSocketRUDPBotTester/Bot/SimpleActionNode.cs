using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.Contents.Client;
using Serilog;

namespace MultiSocketRUDPBotTester.Bot
{
    public class SendPacketNode : ActionNodeBase
    {
        public PacketId PacketId { get; set; }
        public bool Unreliable { get; set; }
        public Func<Client, NetBuffer>? PacketBuilder { get; set; }
        public Dictionary<string, object> FieldValues { get; set; } = new();

        public override void Execute(Client client, NetBuffer? receivedPacket = null)
        {
            try
            {
                var buffer = (PacketBuilder != null ?
                    PacketBuilder(client) : BuildFromSchema()) ?? throw new Exception($"Failed to build packet buffer for PacketId: {PacketId}");

                var token = client.CancellationToken.Token;
                if (token.IsCancellationRequested)
                {
                    return;
                }

                if (PacketId == PacketId.Ping)
                {
                    client.BeginBotRttSample();
                }

                _ = client.SendPacket(buffer, PacketId,
                    Unreliable ? PacketType.UnreliableSendType : PacketType.SendType)
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

        internal NetBuffer? BuildFromSchema()
        {
            var schema = PacketSchema.Get(PacketId);
            if (schema == null)
            {
                return null;
            }

            return BuildFromSchema(schema, FieldValues);
        }

        internal static NetBuffer BuildFromSchema(PacketFieldDef[] schema, IReadOnlyDictionary<string, object> fieldValues)
        {
            // Preserve the small allocation for existing scalar-only packets.
            var hasComposite = schema.Any(field => field.Type is FieldType.Struct or FieldType.Vector
                or FieldType.List or FieldType.Set or FieldType.UnorderedSet or FieldType.Map or FieldType.UnorderedMap);
            var buf = new NetBuffer(hasComposite ? 16384 : 256);
            buf.ReserveHeader();
            foreach (var field in schema)
            {
                var value = fieldValues.TryGetValue(field.Name, out var supplied) ? supplied : field.DefaultValue;
                PacketFieldCodec.Write(buf, field, value);
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
                throw;
            }
        }
    }

    public class DelayNode : ActionNodeBase
    {
        public int DelayMilliseconds { get; set; }

        public override void Execute(Client client, NetBuffer? receivedPacket = null)
        {
            var token = client.CancellationToken.Token;
            Task.Run(async () =>
            {
                try
                {
                    await Task.Delay(DelayMilliseconds, token);
                    Log.Debug("Delayed for {Ms}ms", DelayMilliseconds);

                    var context = client.GlobalContext;
                    context.SetPacket(receivedPacket);
                    var visited = new HashSet<ActionNodeBase>();
                    foreach (var nextNode in NextNodes)
                    {
                        NodeExecutionHelper.ExecuteChain(context, nextNode, receivedPacket, visited);
                    }
                }
                catch (OperationCanceledException) { }
            }, token);
        }
    }

    public class RandomDelayNode : ActionNodeBase
    {
        public int MinDelayMilliseconds { get; set; } = 500;
        public int MaxDelayMilliseconds { get; set; } = 2000;

        public override void Execute(Client client, NetBuffer? receivedPacket = null)
        {
            var delay = SelectDelay(Random.Shared.NextInt64);
            var token = client.CancellationToken.Token;

            Task.Run(async () =>
            {
                try
                {
                    await Task.Delay(delay, token);
                    Log.Debug("RandomDelayNode: Delayed for {Delay}ms (range: {Min}-{Max}ms)", delay, MinDelayMilliseconds, MaxDelayMilliseconds);

                    var context = client.GlobalContext;
                    context.SetPacket(receivedPacket);
                    var visited = new HashSet<ActionNodeBase>();
                    foreach (var nextNode in NextNodes)
                    {
                        NodeExecutionHelper.ExecuteChain(context, nextNode, receivedPacket, visited);
                    }
                }
                catch (OperationCanceledException) { }
            }, token);
        }

        internal int SelectDelay(Func<long, long, long> nextRandom)
        {
            ArgumentNullException.ThrowIfNull(nextRandom);
            if (MinDelayMilliseconds < 0 || MaxDelayMilliseconds < MinDelayMilliseconds)
            {
                throw new ArgumentOutOfRangeException(
                    nameof(MinDelayMilliseconds),
                    "Random delay range must be non-negative and ordered.");
            }

            var exclusiveMaximum = (long)MaxDelayMilliseconds + 1;
            var selected = nextRandom(MinDelayMilliseconds, exclusiveMaximum);
            if (selected < MinDelayMilliseconds || selected >= exclusiveMaximum)
            {
                throw new ArgumentOutOfRangeException(nameof(nextRandom));
            }

            return checked((int)selected);
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
