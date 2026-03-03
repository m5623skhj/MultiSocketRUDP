using MultiSocketRUDPBotTester.Bot;
using Serilog;

namespace MultiSocketRUDPBotTester.Graph.Builders
{
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
}