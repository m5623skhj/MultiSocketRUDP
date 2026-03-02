using MultiSocketRUDPBotTester.Bot;
using MultiSocketRUDPBotTester.Buffer;
using Serilog;

namespace MultiSocketRUDPBotTester.Graph
{
    public interface INodeBuilder
    {
        bool CanBuild(NodeVisual visual);
        ActionNodeBase Build(NodeVisual visual);
    }

    public class SendPacketNodeBuilder : INodeBuilder
    {
        public bool CanBuild(NodeVisual visual) => visual.NodeType == typeof(SendPacketNode);

        public ActionNodeBase Build(NodeVisual visual)
        {
            var packetId = visual.Configuration?.PacketId ?? PacketId.InvalidPacketId;
            if (packetId == PacketId.InvalidPacketId)
            {
                throw new InvalidOperationException(
                    $"SendPacketNode '{visual.NodeType!.Name}' requires a valid PacketId. " +
                    "Please double-click the node to configure it.");
            }

            return new SendPacketNode
            {
                Name = visual.NodeType!.Name,
                PacketId = packetId,
                PacketBuilder = _ => new NetBuffer()
            };
        }
    }

    public class DelayNodeBuilder : INodeBuilder
    {
        public bool CanBuild(NodeVisual visual) => visual.NodeType == typeof(DelayNode);

        public ActionNodeBase Build(NodeVisual visual) => new DelayNode
        {
            Name = visual.NodeType!.Name,
            DelayMilliseconds = visual.Configuration?.IntValue ?? 1000
        };
    }

    public class RandomDelayNodeBuilder : INodeBuilder
    {
        public bool CanBuild(NodeVisual visual) => visual.NodeType == typeof(RandomDelayNode);

        public ActionNodeBase Build(NodeVisual visual) => new RandomDelayNode
        {
            Name = visual.NodeType!.Name,
            MinDelayMilliseconds = visual.Configuration?.Properties.GetValueOrDefault("MinDelay") as int? ?? 500,
            MaxDelayMilliseconds = visual.Configuration?.Properties.GetValueOrDefault("MaxDelay") as int? ?? 2000
        };
    }

    public class DisconnectNodeBuilder : INodeBuilder
    {
        public bool CanBuild(NodeVisual visual) => visual.NodeType == typeof(DisconnectNode);

        public ActionNodeBase Build(NodeVisual visual) => new DisconnectNode
        {
            Name = visual.NodeType!.Name,
            Reason = visual.Configuration?.StringValue ?? "User requested disconnect"
        };
    }

    public class WaitForPacketNodeBuilder : INodeBuilder
    {
        public bool CanBuild(NodeVisual visual) => visual.NodeType == typeof(WaitForPacketNode);

        public ActionNodeBase Build(NodeVisual visual)
        {
            var packetId = visual.Configuration?.PacketId ?? PacketId.InvalidPacketId;
            if (packetId == PacketId.InvalidPacketId)
            {
                throw new InvalidOperationException(
                    "WaitForPacketNode requires a valid PacketId. " +
                    "Please double-click the node to configure it.");
            }

            return new WaitForPacketNode
            {
                Name = visual.NodeType!.Name,
                ExpectedPacketId = packetId,
                TimeoutMilliseconds = visual.Configuration?.IntValue ?? 5000
            };
        }
    }

    public class SetVariableNodeBuilder : INodeBuilder
    {
        public bool CanBuild(NodeVisual visual) => visual.NodeType == typeof(SetVariableNode);

        public ActionNodeBase Build(NodeVisual visual) => new SetVariableNode
        {
            Name = visual.NodeType!.Name,
            VariableName = visual.Configuration?.Properties.GetValueOrDefault("VariableName")?.ToString() ?? "value",
            ValueType = visual.Configuration?.Properties.GetValueOrDefault("ValueType")?.ToString() ?? "int",
            StringValue = visual.Configuration?.Properties.GetValueOrDefault("Value")?.ToString() ?? "0"
        };
    }

    public class GetVariableNodeBuilder : INodeBuilder
    {
        public bool CanBuild(NodeVisual visual) => visual.NodeType == typeof(GetVariableNode);

        public ActionNodeBase Build(NodeVisual visual) => new GetVariableNode
        {
            Name = visual.NodeType!.Name,
            VariableName = visual.Configuration?.StringValue ?? "value"
        };
    }

    public class LogNodeBuilder : INodeBuilder
    {
        public bool CanBuild(NodeVisual visual) => visual.NodeType == typeof(LogNode);

        public ActionNodeBase Build(NodeVisual visual)
        {
            var logMessage = visual.Configuration?.StringValue ?? "No log message configured";
            return new LogNode
            {
                Name = visual.NodeType!.Name,
                MessageBuilder = (client, buffer) =>
                {
                    var msg = logMessage
                        .Replace("{sessionId}", client.GetSessionId().ToString())
                        .Replace("{isConnected}", client.IsConnected().ToString());

                    if (buffer != null)
                    {
                        msg = msg.Replace("{packetSize}", buffer.GetLength().ToString());
                    }

                    return msg;
                }
            };
        }
    }

    public class CustomActionNodeBuilder : INodeBuilder
    {
        public bool CanBuild(NodeVisual visual) => visual.NodeType == typeof(CustomActionNode);

        public ActionNodeBase Build(NodeVisual visual) => new CustomActionNode
        {
            Name = visual.NodeType!.Name,
            ActionHandler = (_, _) => Log.Information("Custom action: {Name}", visual.NodeType.Name)
        };
    }

    public class ConditionalNodeBuilder(Func<RuntimeContext, string?, string, string, string?, string, bool> evaluator)
        : INodeBuilder
    {
        public bool CanBuild(NodeVisual visual) => visual.NodeType == typeof(ConditionalNode);

        public ActionNodeBase Build(NodeVisual visual)
        {
            var leftType = visual.Configuration?.Properties.GetValueOrDefault("LeftType")?.ToString();
            var left = visual.Configuration?.Properties.GetValueOrDefault("Left")?.ToString();
            var op = visual.Configuration?.Properties.GetValueOrDefault("Op")?.ToString();
            var rightType = visual.Configuration?.Properties.GetValueOrDefault("RightType")?.ToString();
            var right = visual.Configuration?.Properties.GetValueOrDefault("Right")?.ToString();

            if (left == null || op == null || right == null)
            {
                throw new InvalidOperationException(
                    "ConditionalNode is not fully configured. Please double-click to configure it.");
            }

            return new ConditionalNode
            {
                Name = visual.NodeType!.Name,
                Condition = ctx => evaluator(ctx, leftType, left, op, rightType, right)
            };
        }
    }

    public class LoopNodeBuilder : INodeBuilder
    {
        public bool CanBuild(NodeVisual visual) => visual.NodeType == typeof(LoopNode);

        public ActionNodeBase Build(NodeVisual visual)
        {
            var count = visual.Configuration?.Properties.GetValueOrDefault("LoopCount") as int? ?? 1;
            var loopId = Guid.NewGuid().ToString();

            return new LoopNode
            {
                Name = visual.NodeType!.Name,
                ContinueCondition = ctx =>
                {
                    var key = $"__loop_{loopId}_index";
                    var i = ctx.GetOrDefault(key, 0);
                    ctx.Set(key, i + 1);
                    return i < count;
                },
                MaxIterations = count
            };
        }
    }

    public class RepeatTimerNodeBuilder : INodeBuilder
    {
        public bool CanBuild(NodeVisual visual) => visual.NodeType == typeof(RepeatTimerNode);

        public ActionNodeBase Build(NodeVisual visual) => new RepeatTimerNode
        {
            Name = visual.NodeType!.Name,
            IntervalMilliseconds = visual.Configuration?.IntValue ?? 1000,
            RepeatCount = visual.Configuration?.Properties.GetValueOrDefault("RepeatCount") as int? ?? 10
        };
    }

    public class PacketParserNodeBuilder : INodeBuilder
    {
        public bool CanBuild(NodeVisual visual) => visual.NodeType == typeof(PacketParserNode);

        public ActionNodeBase Build(NodeVisual visual)
        {
            var setter = visual.Configuration?.Properties.GetValueOrDefault("SetterMethod")?.ToString();
            if (string.IsNullOrEmpty(setter))
            {
                throw new InvalidOperationException(
                    "PacketParserNode requires a setter method. Please double-click to configure it.");
            }

            return new PacketParserNode
            {
                Name = visual.NodeType!.Name,
                SetterMethodName = setter
            };
        }
    }

    public class RandomChoiceNodeBuilder : INodeBuilder
    {
        public bool CanBuild(NodeVisual visual) => visual.NodeType == typeof(RandomChoiceNode);

        public ActionNodeBase Build(NodeVisual visual) => new RandomChoiceNode
        {
            Name = visual.NodeType!.Name,
            Choices = []
        };
    }

    public class AssertNodeBuilder : INodeBuilder
    {
        public bool CanBuild(NodeVisual visual) => visual.NodeType == typeof(AssertNode);

        public ActionNodeBase Build(NodeVisual visual) => new AssertNode
        {
            Name = visual.NodeType!.Name,
            ErrorMessage = visual.Configuration?.StringValue ?? "Assertion failed",
            StopOnFailure = visual.Configuration?.Properties.GetValueOrDefault("StopOnFailure") as bool? ?? true,
            Condition = _ => true
        };
    }

    public class RetryNodeBuilder : INodeBuilder
    {
        public bool CanBuild(NodeVisual visual) => visual.NodeType == typeof(RetryNode);

        public ActionNodeBase Build(NodeVisual visual) => new RetryNode
        {
            Name = visual.NodeType!.Name,
            MaxRetries = visual.Configuration?.Properties.GetValueOrDefault("MaxRetries") as int? ?? 3,
            RetryDelayMilliseconds = visual.Configuration?.Properties.GetValueOrDefault("RetryDelay") as int? ?? 1000,
            UseExponentialBackoff = visual.Configuration?.Properties.GetValueOrDefault("ExponentialBackoff") as bool? ?? false
        };
    }
   
    public class NodeBuilderRegistry(
        Func<RuntimeContext, string?, string, string, string?, string, bool> conditionEvaluator)
    {
        private readonly List<INodeBuilder> builders =
        [
            new SendPacketNodeBuilder(),
            new DelayNodeBuilder(),
            new RandomDelayNodeBuilder(),
            new DisconnectNodeBuilder(),
            new WaitForPacketNodeBuilder(),
            new SetVariableNodeBuilder(),
            new GetVariableNodeBuilder(),
            new LogNodeBuilder(),
            new CustomActionNodeBuilder(),
            new ConditionalNodeBuilder(conditionEvaluator),
            new LoopNodeBuilder(),
            new RepeatTimerNodeBuilder(),
            new PacketParserNodeBuilder(),
            new RandomChoiceNodeBuilder(),
            new AssertNodeBuilder(),
            new RetryNodeBuilder()
        ];

        public ActionNodeBase? TryBuild(NodeVisual visual)
        {
            var builder = builders.FirstOrDefault(b => b.CanBuild(visual));

            if (builder != null)
            {
                return builder.Build(visual);
            }

            if (visual.NodeType == null)
            {
                return null;
            }

            Log.Warning("No builder found for {NodeType}, using CustomActionNode fallback", visual.NodeType.Name);
            return new CustomActionNode
            {
                Name = visual.NodeType.Name,
                ActionHandler = (_, _) => Log.Information("Executing node: {Name}", visual.NodeType.Name)
            };
        }
    }
}