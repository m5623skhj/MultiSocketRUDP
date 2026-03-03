using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Shapes;
using MultiSocketRUDPBotTester.Bot;
using WpfCanvas = System.Windows.Controls.Canvas;

namespace MultiSocketRUDPBotTester.CanvasRenderer
{
    public class NodeCanvasRenderer(WpfCanvas canvas, List<NodeVisual> nodes)
    {
        private const double PortOffsetX = 18.0;
        private const double HalfPortSize = 18.0;

        public void UpdatePortPositions(NodeVisual n)
        {
            var b = n.Border;
            var l = WpfCanvas.GetLeft(b);
            var t = WpfCanvas.GetTop(b);

            SetPosition(n.InputPort, l - PortOffsetX, t + b.Height / 2 - HalfPortSize);
            SetPosition(n.OutputPort, l + b.Width - PortOffsetX, t + b.Height / 2 - HalfPortSize);
            SetPosition(n.OutputPortTrue, l + b.Width - PortOffsetX, t + b.Height / 3 - HalfPortSize);
            SetPosition(n.OutputPortFalse, l + b.Width - PortOffsetX, t + b.Height * 2 / 3 - HalfPortSize);

            if (n.DynamicOutputPorts.Count > 0)
            {
                var spacing = b.Height / (n.DynamicOutputPorts.Count + 1);
                for (var i = 0; i < n.DynamicOutputPorts.Count; i++)
                    SetPosition(n.DynamicOutputPorts[i],
                        l + b.Width - PortOffsetX,
                        t + spacing * (i + 1) - HalfPortSize);
            }

            RedrawConnections();
        }

        private static void SetPosition(FrameworkElement? el, double left, double top)
        {
            if (el == null) return;
            WpfCanvas.SetLeft(el, left);
            WpfCanvas.SetTop(el, top);
        }

        public void RedrawConnections(Line? tempLine = null)
        {
            foreach (var l in canvas.Children.OfType<Line>()
                                             .Where(l => l != tempLine)
                                             .ToList())
            {
                canvas.Children.Remove(l);
            }

            foreach (var n in nodes)
            {
                TryDrawLine(n, n.Next, n.NextPortType);
                TryDrawLine(n, n.TrueChild, n.TruePortType);
                TryDrawLine(n, n.FalseChild, n.FalsePortType);

                for (var i = 0; i < n.DynamicChildren.Count; i++)
                {
                    var child = n.DynamicChildren[i];
                    if (child != null && i < n.DynamicOutputPorts.Count)
                        DrawLine(n, child, n.DynamicOutputPorts[i]);
                }
            }
        }

        private void TryDrawLine(NodeVisual from, NodeVisual? to, string? portType)
        {
            if (to == null) return;
            var port = GetOutputPortByType(from, portType);
            if (port != null) DrawLine(from, to, port);
        }

        private void DrawLine(NodeVisual from, NodeVisual to, FrameworkElement port)
        {
            var p1 = from.GetPortCenter(port, canvas);
            var p2 = to.GetPortCenter(to.InputPort, canvas);

            var line = new Line
            {
                X1 = p1.X,
                Y1 = p1.Y,
                X2 = p2.X,
                Y2 = p2.Y,
                Stroke = Brushes.White,
                StrokeThickness = 3
            };
            Panel.SetZIndex(line, -1);
            canvas.Children.Add(line);
        }

        private static FrameworkElement? GetOutputPortByType(NodeVisual n, string? type) =>
            type switch
            {
                "true" or "continue" => n.OutputPortTrue,
                "false" or "exit" => n.OutputPortFalse,
                null => null,
                _ => n.OutputPort
            };

        public static void Highlight(NodeVisual node)
        {
            node.Border.SetValue(Border.BorderBrushProperty, Brushes.Yellow);
            node.Border.BorderThickness = new Thickness(4);
        }

        public static void Unhighlight(NodeVisual node)
        {
            node.Border.BorderBrush = Brushes.White;
            node.Border.BorderThickness = new Thickness(2);
        }
    }
}