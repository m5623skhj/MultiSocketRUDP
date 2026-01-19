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

            var waitKey = $"__wait_packet_{ExpectedPacketId}_{Guid.NewGuid()}";
            context.Set(waitKey, false);

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

    public class RandomChoiceNode : ActionNodeBase
    {
        public List<ChoiceOption> Choices { get; set; } = [];
        private static readonly Random Random = new();

        public override void Execute(Client client, NetBuffer? receivedPacket = null)
        {
            if (Choices.Count == 0)
            {
                Log.Warning("RandomChoiceNode: No choices defined");
                return;
            }

            var totalWeight = Choices.Sum(c => c.Weight);
            if (totalWeight <= 0)
            {
                Log.Warning("RandomChoiceNode: Total weight is 0 or negative");
                return;
            }

            var randomValue = Random.Next(0, totalWeight);
            var cumulativeWeight = 0;
            ChoiceOption? selectedChoice = null;

            foreach (var choice in Choices)
            {
                cumulativeWeight += choice.Weight;
                if (randomValue >= cumulativeWeight)
                {
                    continue;
                }

                selectedChoice = choice;
                break;
            }

            if (selectedChoice?.Node != null)
            {
                Log.Information($"RandomChoiceNode: Selected '{selectedChoice.Name}' (weight: {selectedChoice.Weight}/{totalWeight})");
                selectedChoice.Node.Execute(client, receivedPacket);
            }
            else
            {
                Log.Warning("RandomChoiceNode: No choice was selected");
            }
        }
    }

    public class ChoiceOption
    {
        public string Name { get; set; } = "";
        public int Weight { get; set; } = 1;
        public ActionNodeBase? Node { get; set; }
    }

    public class AssertNode : ContextNodeBase
    {
        public Func<RuntimeContext, bool>? Condition { get; set; }
        public string? ErrorMessage { get; set; }
        public bool StopOnFailure { get; set; } = true;
        public List<ActionNodeBase> FailureNodes { get; set; } = [];

        protected override void ExecuteImpl(RuntimeContext context)
        {
            if (Condition == null)
            {
                Log.Warning("AssertNode: No condition defined");
                return;
            }

            try
            {
                var result = Condition(context);

                if (result)
                {
                    Log.Information($"AssertNode: Assertion passed - {ErrorMessage ?? "Unnamed assertion"}");

                    foreach (var nextNode in NextNodes)
                    {
                        nextNode.Execute(context.Client, context.Packet);
                    }
                }
                else
                {
                    var message = ErrorMessage ?? "Assertion failed";
                    Log.Error($"AssertNode: ASSERTION FAILED - {message}");

                    foreach (var failureNode in FailureNodes)
                    {
                        failureNode.Execute(context.Client, context.Packet);
                    }

                    if (!StopOnFailure)
                    {
                        return;
                    }

                    Log.Warning("AssertNode: Stopping execution due to assertion failure");
                }
            }
            catch (Exception ex)
            {
                Log.Error($"AssertNode: Exception during assertion - {ex.Message}");

                foreach (var failureNode in FailureNodes)
                {
                    failureNode.Execute(context.Client, context.Packet);
                }

                if (StopOnFailure)
                {
                    throw;
                }
            }
        }
    }

    public class RetryNode : ContextNodeBase
    {
        public int MaxRetries { get; set; } = 3;
        public int RetryDelayMilliseconds { get; set; } = 1000;
        public bool UseExponentialBackoff { get; set; } = false;
        public Func<RuntimeContext, bool>? SuccessCondition { get; set; }
        public List<ActionNodeBase> RetryBody { get; set; } = [];
        public List<ActionNodeBase> SuccessNodes { get; set; } = [];
        public List<ActionNodeBase> FailureNodes { get; set; } = [];

        protected override void ExecuteImpl(RuntimeContext context)
        {
            Task.Run(async () =>
            {
                var attempt = 0;
                var success = false;

                while (attempt < MaxRetries && !success)
                {
                    attempt++;
                    Log.Information($"RetryNode: Attempt {attempt}/{MaxRetries}");

                    try
                    {
                        foreach (var node in RetryBody)
                        {
                            ExecuteNodeChain(context.Client, node, context.Packet);
                        }

                        if (SuccessCondition != null)
                        {
                            success = SuccessCondition(context);
                            Log.Debug($"RetryNode: Success condition evaluated to {success}");
                        }
                        else
                        {
                            success = true;
                        }

                        if (success)
                        {
                            Log.Information($"RetryNode: Success on attempt {attempt}");

                            foreach (var successNode in SuccessNodes)
                            {
                                ExecuteNodeChain(context.Client, successNode, context.Packet);
                            }
                            break;
                        }
                    }
                    catch (Exception ex)
                    {
                        Log.Warning($"RetryNode: Attempt {attempt} failed with exception: {ex.Message}");
                        success = false;
                    }

                    if (attempt >= MaxRetries || success)
                    {
                        continue;
                    }

                    var delay = UseExponentialBackoff
                        ? RetryDelayMilliseconds * (int)Math.Pow(2, attempt - 1)
                        : RetryDelayMilliseconds;

                    Log.Debug($"RetryNode: Waiting {delay}ms before next attempt");
                    await Task.Delay(delay);
                }

                if (!success)
                {
                    Log.Error($"RetryNode: Failed after {MaxRetries} attempts");
                    foreach (var failureNode in FailureNodes)
                    {
                        ExecuteNodeChain(context.Client, failureNode, context.Packet);
                    }
                }

                foreach (var nextNode in NextNodes)
                {
                    ExecuteNodeChain(context.Client, nextNode, context.Packet);
                }
            });
        }

        private static void ExecuteNodeChain(Client client, ActionNodeBase node, NetBuffer? buffer)
        {
            Log.Debug("Executing node: {NodeName}", node.Name);
            node.Execute(client, buffer);

            if (IsAsyncNode(node))
            {
                Log.Debug("Node {NodeName} is async, skipping automatic NextNodes execution", node.Name);
                return;
            }

            if (node.NextNodes.Count > 0)
            {
                Log.Debug("Node {NodeName} has {Count} next nodes", node.Name, node.NextNodes.Count);
            }

            foreach (var nextNode in node.NextNodes)
            {
                ExecuteNodeChain(client, nextNode, buffer);
            }
        }

        private static bool IsAsyncNode(ActionNodeBase node)
        {
            return node is DelayNode
                or RandomDelayNode
                or RepeatTimerNode
                or WaitForPacketNode
                or RetryNode;
        }
    }
}
