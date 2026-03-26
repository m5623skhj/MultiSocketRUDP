using MultiSocketRUDPBotTester.Bot;
using MultiSocketRUDPBotTester.Graph.Builders;
using Serilog;

namespace MultiSocketRUDPBotTester.Graph
{
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
            new AssertNodeBuilder(conditionEvaluator),
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
