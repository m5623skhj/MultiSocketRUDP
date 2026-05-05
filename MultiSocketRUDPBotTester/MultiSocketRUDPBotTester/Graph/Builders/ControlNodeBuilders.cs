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

        /// <summary>
        /// `NodeVisual` 객체에서 정보를 추출하여 `LoopNode` 인스턴스를 생성하고 반환합니다.
        /// `LoopNode`는 특정 횟수만큼 반복 실행되는 로직을 정의합니다.
        /// </summary>
        /// <param name="visual">노드의 시각적 표현 및 구성 데이터를 포함하는 `NodeVisual` 객체.</param>
        /// <returns>생성된 `LoopNode` 인스턴스.</returns>
        /// <remarks>
        /// <para>상태 변화:</para>
        /// <list type="bullet">
        /// <item>새로운 `LoopNode` 인스턴스를 생성하고, `Name` 및 `MaxIterations` 속성을 설정합니다.</item>
        /// <item>`ContinueCondition` 델리게이트는 `BotContext`의 내부 상태를 변경합니다. 루프의 현재 반복 횟수를 추적하기 위해 `__loop_{loopId}_index` 키를 사용하여 `ctx`에 값을 저장하고 증가시킵니다.</item>
        /// </list>
        /// <para>실패 조건:</para>
        /// <list type="bullet">
        /// <item>`visual.NodeType`이 `null`인 경우 `Name` 설정 시 `NullReferenceException`이 발생할 수 있습니다.</item>
        /// <item>`NodeConfigurationValueReader.GetInt`는 "LoopCount" 값이 없거나 유효하지 않으면 기본값 `1`을 사용합니다.</item>
        /// </list>
        /// <para>Side Effects:</para>
        /// <list type="bullet">
        /// <item>반환된 `LoopNode`의 `ContinueCondition` 델리게이트는 실행 시 `BotContext` 객체에 새로운 키-값 쌍을 저장하거나 기존 값을 업데이트하여 컨텍스트의 상태를 변경합니다.</item>
        /// </list>
        /// </remarks>
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
