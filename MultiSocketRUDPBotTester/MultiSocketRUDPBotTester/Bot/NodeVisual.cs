using static MultiSocketRUDPBotTester.BotActionGraphWindow;
using System.Windows.Controls;
using System.Windows;

namespace MultiSocketRUDPBotTester.Bot
{
    public class NodeVisual
    {
        public Border Border = null!;
        public NodeCategory Category;
        public FrameworkElement InputPort = null!;
        public FrameworkElement? OutputPort;
        public FrameworkElement? OutputPortTrue;
        public FrameworkElement? OutputPortFalse;
        public NodeVisual? Next;
        public NodeVisual? TrueChild;
        public NodeVisual? FalseChild;
        public string? NextPortType;
        public string? TruePortType;
        public string? FalsePortType;
        public bool IsRoot;

        public List<FrameworkElement> DynamicOutputPorts = [];
        public List<string> DynamicPortTypes = [];
        public List<NodeVisual?> DynamicChildren = [];

        public ActionNodeBase? ActionNode { get; set; }
        public Type? NodeType;
        public NodeConfiguration? Configuration;

        public NodeRuntimeState RuntimeState;

        public bool HasPort(FrameworkElement p)
        {
            return p == InputPort ||
                   p == OutputPort ||
                   p == OutputPortTrue ||
                   p == OutputPortFalse ||
                   DynamicOutputPorts.Contains(p);
        }

        public Point GetPortCenter(FrameworkElement p, Canvas canvas)
        {
            return p.TranslatePoint(new Point(p.ActualWidth / 2, p.ActualHeight / 2), canvas);
        }
    }

    public class NodeConfiguration
    {
        public PacketId? PacketId { get; set; }
        public string? StringValue { get; set; }
        public int IntValue { get; set; }
        public Dictionary<string, object> Properties { get; set; } = new();
    }
}
