using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Shapes;
using MultiSocketRUDPBotTester.Bot;
using WpfCanvas = System.Windows.Controls.Canvas;

namespace MultiSocketRUDPBotTester.CanvasRenderer
{
    public class NodeInteractionHandler
    {
        private readonly WpfCanvas canvas;
        private readonly ScrollViewer scrollViewer;
        private readonly List<NodeVisual> nodes;
        private readonly NodeCanvasRenderer renderer;
        private readonly Action<string> log;

        private Point dragStart;
        private bool isDragging;
        private bool isPanning;
        private Point panStart;

        public bool IsConnecting { get; private set; }
        private NodeVisual? connectingFromNode;
        private string? connectingPortType;
        private Line? tempConnectionLine;

        private readonly MouseButtonEventHandler _canvasMouseUpHandler;

        public NodeInteractionHandler(
            WpfCanvas canvas,
            ScrollViewer scrollViewer,
            List<NodeVisual> nodes,
            NodeCanvasRenderer renderer,
            Action<string> log)
        {
            this.canvas = canvas;
            this.scrollViewer = scrollViewer;
            this.nodes = nodes;
            this.renderer = renderer;
            this.log = log;

            _canvasMouseUpHandler = (_, _) =>
            {
                isPanning = false;
                canvas.ReleaseMouseCapture();
            };

            SetupCanvasEvents();
        }

        private void SetupCanvasEvents()
        {
            canvas.PreviewMouseMove += OnCanvasMouseMove;
            canvas.PreviewMouseLeftButtonUp += _canvasMouseUpHandler;
            canvas.PreviewMouseLeftButtonDown += OnCanvasLeftButtonDown;
        }

        public void Cleanup()
        {
            canvas.PreviewMouseMove -= OnCanvasMouseMove;
            canvas.PreviewMouseLeftButtonUp -= _canvasMouseUpHandler;
            canvas.PreviewMouseLeftButtonDown -= OnCanvasLeftButtonDown;
            CleanupConnection();
        }

        private void OnCanvasMouseMove(object sender, MouseEventArgs e)
        {
            if (IsConnecting && tempConnectionLine != null)
            {
                var p = e.GetPosition(canvas);
                tempConnectionLine.X2 = p.X;
                tempConnectionLine.Y2 = p.Y;
            }

            if (!isPanning)
            {
                return;
            }

            var pos = e.GetPosition(scrollViewer);
            scrollViewer.ScrollToHorizontalOffset(scrollViewer.HorizontalOffset - (pos.X - panStart.X));
            scrollViewer.ScrollToVerticalOffset(scrollViewer.VerticalOffset - (pos.Y - panStart.Y));
            panStart = pos;
        }

        private void OnCanvasLeftButtonDown(object sender, MouseButtonEventArgs e)
        {
            if (!Equals(e.Source, canvas))
            {
                return;
            }

            isPanning = true;
            panStart = e.GetPosition(scrollViewer);
            canvas.CaptureMouse();
        }

        public void EnableDrag(UIElement element)
        {
            element.MouseLeftButtonDown += (_, ev) =>
            {
                if (IsConnecting)
                {
                    return;
                }

                isDragging = true;
                dragStart = ev.GetPosition(canvas);
                element.CaptureMouse();
            };

            element.MouseMove += (_, ev) =>
            {
                if (!isDragging)
                {
                    return;
                }

                var p = ev.GetPosition(canvas);
                WpfCanvas.SetLeft(element, WpfCanvas.GetLeft(element) + p.X - dragStart.X);
                WpfCanvas.SetTop(element, WpfCanvas.GetTop(element) + p.Y - dragStart.Y);
                dragStart = p;

                var node = nodes.FirstOrDefault(x => x.Border == element);
                if (node != null)
                {
                    renderer.UpdatePortPositions(node);
                }
            };

            element.MouseLeftButtonUp += (_, _) =>
            {
                isDragging = false;
                element.ReleaseMouseCapture();
            };
        }

        public void StartConnection(FrameworkElement port, string type)
        {
            CleanupConnection();

            connectingFromNode = nodes.FirstOrDefault(n => n.HasPort(port));
            if (connectingFromNode == null)
            {
                log($"Warning: Could not find node for port (type: {type})");
                return;
            }

            connectingPortType = type;
            IsConnecting = true;

            var start = connectingFromNode.GetPortCenter(port, canvas);

            tempConnectionLine = new Line
            {
                Stroke = GetPortColor(type),
                StrokeThickness = 3,
                StrokeDashArray = [5, 3],
                X1 = start.X,
                Y1 = start.Y,
                X2 = start.X,
                Y2 = start.Y
            };

            canvas.Children.Add(tempConnectionLine);
            log($"Start connect from {connectingFromNode.Border.Child}");
        }

        public void TryFinishConnection(FrameworkElement port)
        {
            if (!IsConnecting || port.Tag as string != "input")
            {
                return;
            }

            var to = nodes.FirstOrDefault(n => n.InputPort == port);
            if (to == null || to == connectingFromNode)
            {
                CleanupConnection();
                return;
            }

            if (CreatesCycle(connectingFromNode!, to))
            {
                log("Connection would create a cycle, rejected.");
                CleanupConnection();
                return;
            }

            Connect(connectingFromNode!, to, connectingPortType!);
            log($"Connected {connectingFromNode!.Border.Child} → {to.Border.Child}");
            CleanupConnection();
        }

        private void Connect(NodeVisual from, NodeVisual to, string type)
        {
            if (type is "true" or "continue")
            {
                from.TrueChild = to;
                from.TruePortType = type;
            }
            else if (type is "false" or "exit")
            {
                from.FalseChild = to;
                from.FalsePortType = type;
            }
            else if (type.StartsWith("choice_"))
            {
                var index = from.DynamicPortTypes.IndexOf(type);
                if (index >= 0 && index < from.DynamicChildren.Count)
                    from.DynamicChildren[index] = to;
            }
            else
            {
                from.Next = to;
                from.NextPortType = type;
            }

            renderer.RedrawConnections(tempConnectionLine);
        }

        public void CleanupConnection()
        {
            if (tempConnectionLine != null)
            {
                canvas.Children.Remove(tempConnectionLine);
            }

            tempConnectionLine = null;
            connectingFromNode = null;
            connectingPortType = null;
            IsConnecting = false;
        }

        private static bool CreatesCycle(NodeVisual from, NodeVisual to)
        {
            var stack = new Stack<NodeVisual>();
            var visited = new HashSet<NodeVisual>();
            stack.Push(to);

            while (stack.Count > 0)
            {
                var n = stack.Pop();
                if (!visited.Add(n))
                {
                    continue;
                }

                if (n == from)
                {
                    return true;
                }

                if (n.Next != null)
                {
                    stack.Push(n.Next);
                }
                if (n.TrueChild != null)
                {
                    stack.Push(n.TrueChild);
                }
                if (n.FalseChild != null)
                {
                    stack.Push(n.FalseChild);
                }

                foreach (var child in n.DynamicChildren)
                {
                    if (child != null)
                    {
                        stack.Push(child);
                    }
                }
            }

            return false;
        }

        public static Brush GetPortColor(string type) => type switch
        {
            "true" or "continue" => Brushes.LightGreen,
            "false" or "exit" => Brushes.IndianRed,
            _ => Brushes.LightBlue
        };
    }
}
