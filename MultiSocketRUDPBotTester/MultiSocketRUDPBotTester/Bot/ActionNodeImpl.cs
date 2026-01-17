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

                // 다음 노드 실행
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

    public class WaitForPacketNode : ContextNodeBase
    {
        public PacketId ExpectedPacketId { get; set; } = PacketId.INVALID_PACKET_ID;
        public int TimeoutMilliseconds { get; set; } = 5000;
        public List<ActionNodeBase> TimeoutNodes { get; set; } = [];

        private readonly Lock lockObj = new();
        private bool packetReceived;
        private NetBuffer? receivedBuffer;

        protected override void ExecuteImpl(RuntimeContext context)
        {
            if (ExpectedPacketId == PacketId.INVALID_PACKET_ID)
            {
                Log.Warning("WaitForPacketNode: No packet ID specified");
                return;
            }

            Log.Information($"WaitForPacketNode: Waiting for {ExpectedPacketId} (timeout: {TimeoutMilliseconds}ms)");

            var client = context.Client;
            var startTime = CommonFunc.GetNowMs();

            Task.Run(async () =>
            {
                try
                {
                    while (true)
                    {
                        lock (lockObj)
                        {
                            if (packetReceived)
                            {
                                break;
                            }
                        }

                        var elapsed = CommonFunc.GetNowMs() - startTime;
                        if (elapsed >= (ulong)TimeoutMilliseconds)
                        {
                            Log.Warning($"WaitForPacketNode: Timeout waiting for {ExpectedPacketId}");

                            foreach (var timeoutNode in TimeoutNodes)
                            {
                                timeoutNode.Execute(client);
                            }

                            return;
                        }

                        await Task.Delay(50);
                    }

                    foreach (var nextNode in NextNodes)
                    {
                        nextNode.Execute(client, receivedBuffer);
                    }
                }
                catch (Exception ex)
                {
                    Log.Error($"WaitForPacketNode error: {ex.Message}");
                }
            });
            return;

            void OnPacketReceived(PacketId packetId, NetBuffer buffer)
            {
                if (packetId != ExpectedPacketId)
                {
                    return;
                }

                lock (lockObj)
                {
                    if (packetReceived)
                    {
                        return;
                    }

                    packetReceived = true;
                    receivedBuffer = buffer;
                }

                Log.Information($"WaitForPacketNode: Received expected packet {ExpectedPacketId}");
            }
        }
    }

    public class SetVariableNode : ContextNodeBase
    {
        public string VariableName { get; set; } = "";
        public string ValueType { get; set; } = "int";
        public string StringValue { get; set; } = "";

        protected override void ExecuteImpl(RuntimeContext context)
        {
            try
            {
                object value = ValueType.ToLower() switch
                {
                    "int" => int.Parse(StringValue),
                    "long" => long.Parse(StringValue),
                    "float" => float.Parse(StringValue),
                    "double" => double.Parse(StringValue),
                    "bool" => bool.Parse(StringValue),
                    "string" => StringValue,
                    _ => StringValue
                };

                context.Set(VariableName, value);
                Log.Information($"SetVariableNode: Set '{VariableName}' = {value} ({ValueType})");
            }
            catch (Exception ex)
            {
                Log.Error($"SetVariableNode failed: {ex.Message}");
            }
        }
    }

    public class GetVariableNode : ContextNodeBase
    {
        public string VariableName { get; set; } = "";

        protected override void ExecuteImpl(RuntimeContext context)
        {
            try
            {
                if (context.Has(VariableName))
                {
                    var value = context.Get<object>(VariableName);
                    Log.Information($"GetVariableNode: '{VariableName}' = {value}");
                }
                else
                {
                    Log.Warning($"GetVariableNode: Variable '{VariableName}' not found");
                }
            }
            catch (Exception ex)
            {
                Log.Error($"GetVariableNode failed: {ex.Message}");
            }
        }
    }
}
