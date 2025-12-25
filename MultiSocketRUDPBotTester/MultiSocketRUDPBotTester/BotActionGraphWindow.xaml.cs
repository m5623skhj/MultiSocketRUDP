using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Shapes;
using MultiSocketRUDPBotTester.Bot;

namespace MultiSocketRUDPBotTester
{
    public partial class BotActionGraphWindow : Window
    {
        private readonly List<NodeVisual> allNodes = [];
        private Point dragStart;
        private bool isDragging;
        private bool isConnecting;
        private NodeVisual? connectingFromNode;
        private string? connectingPortType;
        private FrameworkElement? connectingFromPort;
        private Line? tempConnectionLine;
        private bool isPanning;
        private Point panStart;
        private NodeVisual? selectedNode;

        public BotActionGraphWindow()
        {
            InitializeComponent();
            LoadActionNodeTypes();
            CreateRootNode();
            SetupCanvasEvents();

            PreviewKeyDown += BotActionGraphWindow_PreviewKeyDown;
        }

        private void SetupCanvasEvents()
        {
            GraphCanvas.PreviewMouseMove += (_, e) =>
            {
                if (isConnecting && tempConnectionLine != null)
                {
                    var p = e.GetPosition(GraphCanvas);
                    tempConnectionLine.X2 = p.X;
                    tempConnectionLine.Y2 = p.Y;
                }

                if (!isPanning)
                {
                    return;
                }

                {
                    var p = e.GetPosition(GraphScroll);
                    var dx = p.X - panStart.X;
                    var dy = p.Y - panStart.Y;

                    GraphScroll.ScrollToHorizontalOffset(GraphScroll.HorizontalOffset - dx);
                    GraphScroll.ScrollToVerticalOffset(GraphScroll.VerticalOffset - dy);

                    panStart = p;
                }
            };

            GraphCanvas.PreviewMouseLeftButtonUp += (_, _) =>
            {
                isPanning = false;
                GraphCanvas.ReleaseMouseCapture();
            };

            GraphCanvas.PreviewMouseLeftButtonDown += (_, e) =>
            {
                if (!Equals(e.Source, GraphCanvas))
                {
                    return;
                }

                isPanning = true;
                panStart = e.GetPosition(GraphScroll);
                GraphCanvas.CaptureMouse();
            };
        }

        private void CreateRootNode()
        {
            var b = CreateNodeVisual("OnConnected", Brushes.DarkGreen);
            var n = new NodeVisual()
            {
                Border = b,
                Category = NodeCategory.Action,
                InputPort = CreateInputPort(),
                OutputPort = CreateOutputPort("default"),
                IsRoot = true
            };

            Canvas.SetLeft(b, 50);
            Canvas.SetTop(b, 200);
            AddNodeToCanvas(n);
            Log("Root node created.");
        }

        private void LoadActionNodeTypes()
        {
            var baseType = typeof(ActionNodeBase);
            ActionNodeListBox.ItemsSource = baseType.Assembly.GetTypes()
                .Where(t => t is { IsClass: true, IsAbstract: false } && baseType.IsAssignableFrom(t))
                .ToList();
        }

        private Border CreateNodeVisual(string title, Brush color)
        {
            var t = new TextBlock
            {
                Text = title, 
                Foreground = Brushes.White, 
                HorizontalAlignment = HorizontalAlignment.Center, 
                VerticalAlignment = VerticalAlignment.Center
            };
            var b = new Border
            {
                Width = 140, 
                Height = 60, 
                Background = color, 
                BorderBrush = Brushes.White, 
                BorderThickness = new Thickness(2), 
                Child = t
            };

            EnableDrag(b);
            return b;
        }

        private void AttachNodeContextMenu(NodeVisual node)
        {
            var menu = new ContextMenu();
            var deleteItem = new MenuItem
            {
                Header = "삭제",
                IsEnabled = !node.IsRoot
            };

            deleteItem.Click += (_, _) =>
            {
                if (!node.IsRoot)
                {
                    DeleteNode(node);
                }
            };

            menu.Items.Add(deleteItem);
            node.Border.ContextMenu = menu;
        }

        private NodeVisual? FindNode(Border b)
        {
            return allNodes.FirstOrDefault(n => n.Border == b);
        }

        private void DeleteNodeByBorder(Border b)
        {
            var node = FindNode(b);
            if (node == null)
            {
                return;
            }

            DeleteNode(node);
        }

        private FrameworkElement CreateInputPort()
        {
            var hit = new Grid { Width = 40, Height = 40, Background = Brushes.Transparent, Tag = "input" };
            var circle = new Ellipse { Width = 24, Height = 24, Fill = Brushes.LightGreen, Stroke = Brushes.White, StrokeThickness = 3, IsHitTestVisible = false };
            hit.Children.Add(circle);
            Panel.SetZIndex(hit, 1000);
            hit.MouseLeftButtonUp += InputPort_MouseUp;

            return hit;
        }

        private FrameworkElement CreateOutputPort(string type)
        {
            var hit = new Grid { Width = 40, Height = 40, Background = Brushes.Transparent, Tag = type };
            var circle = new Ellipse { Width = 24, Height = 24, Fill = GetPortColor(type), Stroke = Brushes.White, StrokeThickness = 3, IsHitTestVisible = false };
            hit.Children.Add(circle);
            Panel.SetZIndex(hit, 1000);
            hit.MouseLeftButtonDown += (_, _) => StartConnection(hit, type);

            return hit;
        }


        private static Brush GetPortColor(string type) => type switch
        {
            "true" or "continue" => Brushes.LightGreen,
            "false" or "exit" => Brushes.IndianRed,
            _ => Brushes.LightBlue
        };

        private void StartConnection(FrameworkElement port, string type)
        {
            CleanupConnection();

            connectingFromNode = allNodes.First(n => n.HasPort(port));
            connectingFromPort = port;
            connectingPortType = type;
            isConnecting = true;

            tempConnectionLine = new Line { Stroke = GetPortColor(type), StrokeThickness = 3, StrokeDashArray = [5, 3] };
            var start = connectingFromNode.GetPortCenter(port, GraphCanvas);

            tempConnectionLine.X1 = start.X;
            tempConnectionLine.Y1 = start.Y;
            tempConnectionLine.X2 = start.X;
            tempConnectionLine.Y2 = start.Y;

            GraphCanvas.Children.Add(tempConnectionLine);
            Log($"Start connect from {connectingFromNode.Border.Child}");
        }

        private void InputPort_MouseUp(object sender, MouseButtonEventArgs e)
        {
            e.Handled = true;

            if (!isConnecting || sender is not FrameworkElement port || port.Tag as string != "input")
            {
                return;
            }

            var to = allNodes.First(n => n.InputPort == port);
            if (to == connectingFromNode)
            {
                return;
            }

            if (CreatesCycle(connectingFromNode!, to))
            {
                return;
            }

            Connect(connectingFromNode!, to, connectingPortType!);
            Log($"Connected {connectingFromNode} -> {to}");
            CleanupConnection();
        }

        private bool CreatesCycle(NodeVisual from, NodeVisual to)
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
            }

            return false;
        }

        private void Connect(NodeVisual from, NodeVisual to, string type)
        {
            if (type is "true" or "continue")
            {
                from.TrueChild = to;
            }
            else if (type is "false" or "exit")
            {
                from.FalseChild = to;
            }
            else
            {
                from.Next = to;
            }

            RedrawConnections();
        }

        private void DisconnectIncoming(NodeVisual node)
        {
            foreach (var n in allNodes)
            {
                if (n.Next == node)
                {
                    n.Next = null;
                }

                if (n.TrueChild == node)
                {
                    n.TrueChild = null;
                }

                if (n.FalseChild == node)
                {
                    n.FalseChild = null;
                }
            }
        }

        private void CleanupConnection()
        {
            if (tempConnectionLine != null)
            {
                GraphCanvas.Children.Remove(tempConnectionLine);
            }

            tempConnectionLine = null;
            connectingFromNode = null;
            connectingFromPort = null;
            connectingPortType = null;
            isConnecting = false;
        }

        private void AddNodeToCanvas(NodeVisual node)
        {
            AttachNodeContextMenu(node);

            GraphCanvas.Children.Add(node.Border);
            GraphCanvas.Children.Add(node.InputPort);

            if (node.OutputPort != null)
            {
                GraphCanvas.Children.Add(node.OutputPort);
            }

            if (node.OutputPortTrue != null)
            {
                GraphCanvas.Children.Add(node.OutputPortTrue);
            }

            if (node.OutputPortFalse != null)
            {
                GraphCanvas.Children.Add(node.OutputPortFalse);
            }

            allNodes.Add(node);
            UpdatePortPositions(node);
        }

        private void UpdatePortPositions(NodeVisual n)
        {
            var b = n.Border;
            var l = Canvas.GetLeft(b);
            var t = Canvas.GetTop(b);

            Canvas.SetLeft(n.InputPort, l - 18);
            Canvas.SetTop(n.InputPort, t + b.Height / 2 - 18);

            if (n.OutputPort != null)
            {
                Canvas.SetLeft(n.OutputPort, l + b.Width - 18);
                Canvas.SetTop(n.OutputPort, t + b.Height / 2 - 18);
            }

            if (n.OutputPortTrue != null)
            {
                Canvas.SetLeft(n.OutputPortTrue, l + b.Width - 18);
                Canvas.SetTop(n.OutputPortTrue, t + b.Height / 3 - 18);
            }

            if (n.OutputPortFalse != null)
            {
                Canvas.SetLeft(n.OutputPortFalse, l + b.Width - 18);
                Canvas.SetTop(n.OutputPortFalse, t + b.Height * 2 / 3 - 18);
            }

            RedrawConnections();
        }

        private void SelectNodeByBorder(Border b)
        {
            selectedNode = allNodes.FirstOrDefault(n => n.Border == b);

            foreach (var n in allNodes)
                n.Border.BorderBrush = Brushes.White;

            if (selectedNode != null)
                selectedNode.Border.BorderBrush = Brushes.Yellow;
        }

        private void RedrawConnections()
        {
            foreach (var l in GraphCanvas.Children.OfType<Line>().Where(l => l != tempConnectionLine).ToList())
                GraphCanvas.Children.Remove(l);

            foreach (var n in allNodes)
            {
                if (n.Next != null)
                {
                    DrawLine(n, n.Next, n.OutputPort!);
                }

                if (n.TrueChild != null)
                {
                    DrawLine(n, n.TrueChild, n.OutputPortTrue!);
                }

                if (n.FalseChild != null)
                {
                    DrawLine(n, n.FalseChild, n.OutputPortFalse!);
                }
            }
        }

        private void DrawLine(NodeVisual from, NodeVisual to, FrameworkElement port)
        {
            var p1 = from.GetPortCenter(port, GraphCanvas);
            var p2 = to.GetPortCenter(to.InputPort, GraphCanvas);
            var l = new Line { X1 = p1.X, Y1 = p1.Y, X2 = p2.X, Y2 = p2.Y, Stroke = Brushes.White, StrokeThickness = 3 };
            Panel.SetZIndex(l, -1);
            GraphCanvas.Children.Add(l);
        }

        private void EnableDrag(UIElement e)
        {
            e.MouseLeftButtonDown += (_, ev) =>
            {
                if (isConnecting) return;
                isDragging = true;
                dragStart = ev.GetPosition(GraphCanvas);
                e.CaptureMouse();
            };

            e.MouseMove += (_, ev) =>
            {
                if (!isDragging) return;
                var p = ev.GetPosition(GraphCanvas);
                Canvas.SetLeft(e, Canvas.GetLeft(e) + p.X - dragStart.X);
                Canvas.SetTop(e, Canvas.GetTop(e) + p.Y - dragStart.Y);
                dragStart = p;

                var n = allNodes.FirstOrDefault(x => x.Border == e);
                if (n != null)
                {
                    UpdatePortPositions(n);
                }
            };

            e.MouseLeftButtonUp += (_, _) =>
            {
                isDragging = false;
                e.ReleaseMouseCapture();
            };
        }

        private void AddNode_Click(object sender, RoutedEventArgs e)
        {
            if (ActionNodeListBox.SelectedItem is not Type t)
            {
                return;
            }

            var cat = t.Name.Contains("Condition") ? NodeCategory.Condition :
                      t.Name.Contains("Loop") ? NodeCategory.Loop : NodeCategory.Action;

            var b = CreateNodeVisual(t.Name, GetNodeColor(cat));
            var n = new NodeVisual { Border = b, Category = cat, InputPort = CreateInputPort() };

            if (cat == NodeCategory.Action)
            {
                n.OutputPort = CreateOutputPort("default");
            }
            else
            {
                n.OutputPortTrue = CreateOutputPort(cat == NodeCategory.Condition ? "true" : "continue");
                n.OutputPortFalse = CreateOutputPort(cat == NodeCategory.Condition ? "false" : "exit");
            }

            Canvas.SetLeft(b, GraphScroll.HorizontalOffset + 400);
            Canvas.SetTop(b, GraphScroll.VerticalOffset + 300);
            AddNodeToCanvas(n);
            Log($"Node added: {t.Name}");
        }

        private static Brush GetNodeColor(NodeCategory c) => c switch
        {
            NodeCategory.Condition => Brushes.DarkOrange,
            NodeCategory.Loop => Brushes.DarkMagenta,
            _ => Brushes.DimGray
        };

        public enum NodeRuntimeState
        {
            Idle,
            Running,
            Success,
            Fail
        }

        private void DeleteNode(NodeVisual node)
        {
            if (node.IsRoot)
            {
                Log("Root node cannot be deleted.");
                return;
            }

            DisconnectIncoming(node);

            GraphCanvas.Children.Remove(node.Border);
            GraphCanvas.Children.Remove(node.InputPort);

            if (node.OutputPort != null)
            {
                GraphCanvas.Children.Remove(node.OutputPort);
            }

            if (node.OutputPortTrue != null)
            {
                GraphCanvas.Children.Remove(node.OutputPortTrue);
            }

            if (node.OutputPortFalse != null)
            {
                GraphCanvas.Children.Remove(node.OutputPortFalse);
            }

            allNodes.Remove(node);

            RedrawConnections();
            Log($"Node deleted: {((TextBlock)node.Border.Child).Text}");
        }

        private void BotActionGraphWindow_PreviewKeyDown(object sender, KeyEventArgs e)
        {
            if (e.Key != Key.Delete || selectedNode == null)
            {
                return;
            }

            DeleteNode(selectedNode);
            selectedNode = null;
        }

        public enum NodeCategory { Action, Condition, Loop }

        private static Brush GetRuntimeBrush(NodeRuntimeState s) => s switch
        {
            NodeRuntimeState.Running => Brushes.LimeGreen,
            NodeRuntimeState.Success => Brushes.DodgerBlue,
            NodeRuntimeState.Fail => Brushes.IndianRed,
            _ => Brushes.White
        };

        private void UpdateNodeVisualState(NodeVisual n)
        {
            n.Border.BorderBrush = GetRuntimeBrush(n.RuntimeState);
        }

        private void Log(string msg)
        {
            LogListBox.Items.Add($"[{DateTime.Now:HH:mm:ss}] {msg}");
            LogListBox.ScrollIntoView(LogListBox.Items[^1]);
        }
    }
}
