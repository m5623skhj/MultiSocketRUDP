using MultiSocketRUDPBotTester.Buffer;
using Serilog;
using System.Globalization;

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

            var cancellationToken = context.Client.CancellationToken.Token;
            var waiterTask = context.Client.WaitForNextPacketAsync(
                ExpectedPacketId, TimeoutMilliseconds, cancellationToken);

            context.SetPendingAsyncTask(WaitAndDispatchAsync(context, waiterTask));
        }

        private async Task WaitAndDispatchAsync(RuntimeContext context, Task<NetBuffer?> waiterTask)
        {
            try
            {
                Log.Information("WaitForPacketNode: Waiting for {Id} (timeout: {Timeout}ms)",
                    ExpectedPacketId, TimeoutMilliseconds);
                var receivedBuffer = await waiterTask.ConfigureAwait(false);

                if (receivedBuffer != null)
                {
                    Log.Information("WaitForPacketNode: Successfully received {Id}", ExpectedPacketId);
                    var visited = new HashSet<ActionNodeBase>();
                    foreach (var nextNode in NextNodes)
                        NodeExecutionHelper.ExecuteChain(context, nextNode, visited);
                }
                else
                {
                    Log.Warning("WaitForPacketNode: Timeout waiting for {Id}", ExpectedPacketId);
                    var timeoutVisited = new HashSet<ActionNodeBase>();
                    foreach (var timeoutNode in TimeoutNodes)
                        NodeExecutionHelper.ExecuteChain(context, timeoutNode, timeoutVisited);
                }
            }
            catch (OperationCanceledException)
            {
                Log.Information("WaitForPacketNode: Cancelled for {Id}", ExpectedPacketId);
            }
            catch (Exception ex)
            {
                Log.Error("WaitForPacketNode error: {Message}", ex.Message);
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
                    "int" => int.Parse(StringValue, CultureInfo.InvariantCulture),
                    "long" => long.Parse(StringValue, CultureInfo.InvariantCulture),
                    "float" => float.Parse(StringValue, CultureInfo.InvariantCulture),
                    "double" => double.Parse(StringValue, CultureInfo.InvariantCulture),
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
