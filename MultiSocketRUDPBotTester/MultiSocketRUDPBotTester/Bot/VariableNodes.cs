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
                Log.Information($"WaitForPacketNode: Packet {ExpectedPacketId} already received");

                foreach (var nextNode in NextNodes)
                {
                    nextNode.Execute(context.Client, buffer);
                }
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
                        if (context.Has(receivedKey))
                        {
                            var buffer = context.Get<NetBuffer>(receivedKey);
                            Log.Information($"WaitForPacketNode: Received expected packet {ExpectedPacketId}");

                            foreach (var nextNode in NextNodes)
                            {
                                nextNode.Execute(client, buffer);
                            }
                            return;
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
                }
                catch (Exception ex)
                {
                    Log.Error($"WaitForPacketNode error: {ex.Message}");
                }
            });
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