using MultiSocketRUDPBotTester.Bot;

namespace MultiSocketRUDPBotTester.Graph.Builders
{
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
            var count = NodeConfigurationValueReader.GetInt(visual, "LoopCount", 1);
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
            RepeatCount = NodeConfigurationValueReader.GetInt(visual, "RepeatCount", 10)
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

    public class AssertNodeBuilder(Func<RuntimeContext, string?, string, string, string?, string, bool> evaluator)
        : INodeBuilder
    {
        public bool CanBuild(NodeVisual visual) => visual.NodeType == typeof(AssertNode);

        public ActionNodeBase Build(NodeVisual visual)
        {
            var leftType = visual.Configuration?.Properties.GetValueOrDefault("LeftType")?.ToString();
            var left = visual.Configuration?.Properties.GetValueOrDefault("Left")?.ToString() ?? "";
            var op = visual.Configuration?.Properties.GetValueOrDefault("Op")?.ToString() ?? "==";
            var rightType = visual.Configuration?.Properties.GetValueOrDefault("RightType")?.ToString();
            var right = visual.Configuration?.Properties.GetValueOrDefault("Right")?.ToString() ?? "";

            return new AssertNode
            {
                Name = visual.NodeType!.Name,
                ErrorMessage = visual.Configuration?.StringValue ?? "Assertion failed",
                StopOnFailure = NodeConfigurationValueReader.GetBool(visual, "StopOnFailure", true),
                Condition = ctx => evaluator(ctx, leftType, left, op, rightType, right)
            };
        }
    }

    public class RetryNodeBuilder : INodeBuilder
    {
        public bool CanBuild(NodeVisual visual) => visual.NodeType == typeof(RetryNode);

        public ActionNodeBase Build(NodeVisual visual) => new RetryNode
        {
            Name = visual.NodeType!.Name,
            MaxRetries = NodeConfigurationValueReader.GetInt(visual, "MaxRetries", 3),
            RetryDelayMilliseconds = NodeConfigurationValueReader.GetInt(visual, "RetryDelay", 1000),
            UseExponentialBackoff = NodeConfigurationValueReader.GetBool(visual, "ExponentialBackoff", false)
        };
    }
}
