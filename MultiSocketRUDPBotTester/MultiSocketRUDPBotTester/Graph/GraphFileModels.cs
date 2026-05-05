using System.Text.Json;
using MultiSocketRUDPBotTester.Bot;

namespace MultiSocketRUDPBotTester.Graph
{
    public class GraphFileModel
    {
        public string Name { get; set; } = "Bot Action Graph";
        public List<NodeVisualFileModel> Nodes { get; set; } = [];
    }

    public class NodeVisualFileModel
    {
        public int Id { get; set; }
        public bool IsRoot { get; set; }
        public string? NodeTypeName { get; set; }
        public BotActionGraphWindow.NodeCategory Category { get; set; }
        public double Left { get; set; }
        public double Top { get; set; }
        public NodeConfigurationFileModel? Configuration { get; set; }
        public string? NextPortType { get; set; }
        public string? TruePortType { get; set; }
        public string? FalsePortType { get; set; }
        public int? NextNodeId { get; set; }
        public int? TrueChildId { get; set; }
        public int? FalseChildId { get; set; }
        public List<string> DynamicPortTypes { get; set; } = [];
        public List<int?> DynamicChildIds { get; set; } = [];
    }

    public class NodeConfigurationFileModel
    {
        public PacketId? PacketId { get; set; }
        public string? StringValue { get; set; }
        public int IntValue { get; set; }
        public Dictionary<string, JsonElement> Properties { get; set; } = [];
    }
}
