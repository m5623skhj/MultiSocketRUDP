using static MultiSocketRUDPBotTester.BotActionGraphWindow;
using System.Windows.Controls;
using System.Windows;

namespace MultiSocketRUDPBotTester.Bot
{
    public class NodeVisual
    {
        public Border Border { get; set; } = null!;
        public NodeCategory Category { get; set; }
        public FrameworkElement InputPort { get; set; } = null!;
        public FrameworkElement? OutputPort { get; set; }
        public FrameworkElement? OutputPortTrue { get; set; }
        public FrameworkElement? OutputPortFalse { get; set; }

        public List<FrameworkElement> DynamicOutputPorts { get; set; } = [];
        public List<string> DynamicPortTypes { get; set; } = [];
        public List<NodeVisual?> DynamicChildren { get; set; } = [];

        public NodeVisual? Next { get; set; }
        public NodeVisual? TrueChild { get; set; }
        public NodeVisual? FalseChild { get; set; }
        public string? NextPortType { get; set; }
        public string? TruePortType { get; set; }
        public string? FalsePortType { get; set; }

        public bool IsRoot { get; set; }
        public Type? NodeType { get; set; }
        public NodeConfiguration? Configuration { get; set; }
        public ActionNodeBase? ActionNode { get; set; }
        public NodeRuntimeState RuntimeState { get; set; }

        public bool HasPort(FrameworkElement p) =>
            p == InputPort ||
            p == OutputPort ||
            p == OutputPortTrue ||
            p == OutputPortFalse ||
            DynamicOutputPorts.Contains(p);

        public Point GetPortCenter(FrameworkElement p, Canvas canvas) =>
            p.TranslatePoint(new Point(p.ActualWidth / 2, p.ActualHeight / 2), canvas);
    }

    public class NodeConfiguration
    {
        public PacketId? PacketId { get; set; }
        public string? StringValue { get; set; }
        public int IntValue { get; set; }
        public Dictionary<string, object> Properties { get; set; } = new();
    }
}