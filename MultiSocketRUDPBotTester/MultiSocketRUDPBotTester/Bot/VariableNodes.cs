using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.Contents.Client;
using Serilog;

namespace MultiSocketRUDPBotTester.Bot
{
    public class WaitForPacketNode : ContextNodeBase
    {
        public PacketId ExpectedPacketId { get; set; } = PacketId.InvalidPacketId;
        public int TimeoutMilliseconds { get; set; } = 5000;
        public List<ActionNodeBase> TimeoutNodes { get; set; } = [];

        protected override void ExecuteImpl(RuntimeContext context)
        {
            if (ExpectedPacketId == PacketId.InvalidPacketId)
            {
                Log.Warning("WaitForPacketNode: No packet ID specified");
                return;
            }

            var receivedKey = $"__received_{ExpectedPacketId}";

            if (context.Has(receivedKey))
            {
                var buffer = context.Get<NetBuffer>(receivedKey);
                Log.Information("WaitForPacketNode: Packet {Id} already received", ExpectedPacketId);

                var visited = new HashSet<ActionNodeBase>();
                foreach (var nextNode in NextNodes)
                {
                    NodeExecutionHelper.ExecuteChain(context, nextNode, visited);
                }
                return;
            }

            Log.Information("WaitForPacketNode: Waiting for {Id} (timeout: {Timeout}ms)",
                ExpectedPacketId, TimeoutMilliseconds);

            var client = context.Client;
            var startTime = CommonFunc.GetNowMs();

            var cancellationToken = client.CancellationToken.Token;

            Task.Run(async () =>
            {
                try
                {
                    while (true)
                    {
                        if (cancellationToken.IsCancellationRequested)
                        {
                            Log.Information("WaitForPacketNode: Cancelled while waiting for {Id}", ExpectedPacketId);
                            return;
                        }

                        if (context.Has(receivedKey))
                        {
                            var buffer = context.Get<NetBuffer>(receivedKey);
                            Log.Information("WaitForPacketNode: Received expected packet {Id}", ExpectedPacketId);

                            var visited = new HashSet<ActionNodeBase>();
                            foreach (var nextNode in NextNodes)
                            {
                                NodeExecutionHelper.ExecuteChain(context, nextNode, visited);
                            }
                            return;
                        }

                        var elapsed = CommonFunc.GetNowMs() - startTime;
                        if (elapsed >= (ulong)TimeoutMilliseconds)
                        {
                            Log.Warning("WaitForPacketNode: Timeout waiting for {Id}", ExpectedPacketId);

                            var timeoutVisited = new HashSet<ActionNodeBase>();
                            foreach (var timeoutNode in TimeoutNodes)
                            {
                                NodeExecutionHelper.ExecuteChain(context, timeoutNode, timeoutVisited);
                            }
                            return;
                        }

                        await Task.Delay(50, cancellationToken);
                    }
                }
                catch (OperationCanceledException)
                {
                    Log.Information("WaitForPacketNode: Cancelled");
                }
                catch (Exception ex)
                {
                    Log.Error("WaitForPacketNode error: {Message}", ex.Message);
                }
            }, cancellationToken);
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
                Log.Information("SetVariableNode: Set '{Name}' = {Value} ({Type})",
                    VariableName, value, ValueType);
            }
            catch (Exception ex)
            {
                Log.Error("SetVariableNode failed: {Message}", ex.Message);
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
                    Log.Information("GetVariableNode: '{Name}' = {Value}", VariableName, value);
                }
                else
                {
                    Log.Warning("GetVariableNode: Variable '{Name}' not found", VariableName);
                }
            }
            catch (Exception ex)
            {
                Log.Error("GetVariableNode failed: {Message}", ex.Message);
            }
        }
    }
}
