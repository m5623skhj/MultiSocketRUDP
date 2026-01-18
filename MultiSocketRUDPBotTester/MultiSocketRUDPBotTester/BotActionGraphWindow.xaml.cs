using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Shapes;
using MultiSocketRUDPBotTester.Bot;
using MultiSocketRUDPBotTester.ClientCore;

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
        private const double PortOffsetX = 18.0;
        private const double HalfPortSize = 18.0;

        public ActionGraph? BuiltGraph { get; private set; }

        public BotActionGraphWindow()
        {
            InitializeComponent();
            LoadActionNodeTypes();

            var savedVisuals = BotTesterCore.Instance.GetSavedGraphVisuals();
            if (savedVisuals is { Count: > 0 })
            {
                RestoreSavedGraph(savedVisuals);
            }
            else
            {
                CreateRootNode();
            }

            SetupCanvasEvents();

            PreviewKeyDown += BotActionGraphWindow_PreviewKeyDown;
        }

        private void RestoreSavedGraph(List<NodeVisual> savedVisuals)
        {
            var nodeMapping = new Dictionary<NodeVisual, NodeVisual>();
            var positionMapping = new Dictionary<NodeVisual, (double left, double top)>();

            foreach (var savedNode in savedVisuals)
            {
                var left = Canvas.GetLeft(savedNode.Border);
                var top = Canvas.GetTop(savedNode.Border);
                positionMapping[savedNode] = (left, top);
            }

            foreach (var savedNode in savedVisuals)
            {
                var newNode = CloneNodeVisual(savedNode, positionMapping[savedNode]);
                nodeMapping[savedNode] = newNode;

                AttachNodeContextMenu(newNode);
                GraphCanvas.Children.Add(newNode.Border);
                GraphCanvas.Children.Add(newNode.InputPort);

                if (newNode.OutputPort != null)
                {
                    GraphCanvas.Children.Add(newNode.OutputPort);
                }

                if (newNode.OutputPortTrue != null)
                {
                    GraphCanvas.Children.Add(newNode.OutputPortTrue);
                }

                if (newNode.OutputPortFalse != null)
                {
                    GraphCanvas.Children.Add(newNode.OutputPortFalse);
                }

                allNodes.Add(newNode);

                var b = newNode.Border;
                var l = Canvas.GetLeft(b);
                var t = Canvas.GetTop(b);

                Canvas.SetLeft(newNode.InputPort, l - PortOffsetX);
                Canvas.SetTop(newNode.InputPort, t + b.Height / 2 - HalfPortSize);

                if (newNode.OutputPort != null)
                {
                    Canvas.SetLeft(newNode.OutputPort, l + b.Width - PortOffsetX);
                    Canvas.SetTop(newNode.OutputPort, t + b.Height / 2 - HalfPortSize);
                }

                if (newNode.OutputPortTrue != null)
                {
                    Canvas.SetLeft(newNode.OutputPortTrue, l + b.Width - PortOffsetX);
                    Canvas.SetTop(newNode.OutputPortTrue, t + b.Height / 3 - HalfPortSize);
                }

                if (newNode.OutputPortFalse != null)
                {
                    Canvas.SetLeft(newNode.OutputPortFalse, l + b.Width - PortOffsetX);
                    Canvas.SetTop(newNode.OutputPortFalse, t + b.Height * 2 / 3 - HalfPortSize);
                }
            }

            foreach (var savedNode in savedVisuals)
            {
                if (!nodeMapping.TryGetValue(savedNode, out var newNode))
                {
                    continue;
                }

                if (savedNode.Next != null && nodeMapping.TryGetValue(savedNode.Next, out var nextNode))
                {
                    newNode.Next = nextNode;
                    Log($"Restored connection: {((TextBlock)newNode.Border.Child).Text} -> {((TextBlock)nextNode.Border.Child).Text}");
                }

                if (savedNode.TrueChild != null && nodeMapping.TryGetValue(savedNode.TrueChild, out var trueNode))
                {
                    newNode.TrueChild = trueNode;
                    Log($"Restored true branch: {((TextBlock)newNode.Border.Child).Text} -> {((TextBlock)trueNode.Border.Child).Text}");
                }

                if (savedNode.FalseChild != null && nodeMapping.TryGetValue(savedNode.FalseChild, out var falseNode))
                {
                    newNode.FalseChild = falseNode;
                    Log($"Restored false branch: {((TextBlock)newNode.Border.Child).Text} -> {((TextBlock)falseNode.Border.Child).Text}");
                }
            }

            Dispatcher.BeginInvoke(new Action(RedrawConnections), System.Windows.Threading.DispatcherPriority.Loaded);
            Log($"Graph restored with {allNodes.Count} nodes and connections.");
        }

        private NodeVisual CloneNodeVisual(NodeVisual original, (double left, double top) position)
        {
            var color = original.IsRoot ? Brushes.DarkGreen : GetNodeColor(original.Category);
            var title = original.IsRoot ? "OnConnected" : (original.NodeType?.Name ?? "Unknown");
            var border = CreateNodeVisual(title, color);

            var newNode = new NodeVisual
            {
                Border = border,
                Category = original.Category,
                InputPort = CreateInputPort(),
                IsRoot = original.IsRoot,
                NodeType = original.NodeType,
                Configuration = CloneConfiguration(original.Configuration),
                NextPortType = original.NextPortType,
                TruePortType = original.TruePortType,
                FalsePortType = original.FalsePortType
            };

            if (original is { ActionNode: not null, IsRoot: true })
            {
                newNode.ActionNode = new CustomActionNode
                {
                    Name = "OnConnected",
                    Trigger = new TriggerCondition { Type = TriggerType.OnConnected }
                };
            }

            if (original.Category == NodeCategory.Action)
            {
                newNode.OutputPort = CreateOutputPort("default");
            }
            else
            {
                newNode.OutputPortTrue = CreateOutputPort(original.Category == NodeCategory.Condition ? "true" : "continue");
                newNode.OutputPortFalse = CreateOutputPort(original.Category == NodeCategory.Condition ? "false" : "exit");
            }

            Canvas.SetLeft(border, position.left);
            Canvas.SetTop(border, position.top);

            return newNode;
        }

        private static NodeConfiguration? CloneConfiguration(NodeConfiguration? original)
        {
            if (original == null)
            {
                return null;
            }

            return new NodeConfiguration
            {
                PacketId = original.PacketId,
                StringValue = original.StringValue,
                IntValue = original.IntValue,
                Properties = new Dictionary<string, object>(original.Properties)
            };
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

            GraphCanvas.MouseLeftButtonDown += (_, _) =>
            {
                if (selectedNode == null)
                {
                    return;
                }

                Unhighlight(selectedNode);
                selectedNode = null;
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
                IsRoot = true,
                ActionNode = new CustomActionNode
                {
                    Name = "OnConnected",
                    Trigger = new TriggerCondition { Type = TriggerType.OnConnected }
                }
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
                BorderThickness = new Thickness(2),
                BorderBrush = Brushes.White,
                Child = t
            };

            EnableDrag(b);

            b.PreviewMouseLeftButtonDown += Border_PreviewMouseLeftButtonDown;
            b.MouseLeftButtonDown += (_, e) =>
            {
                SelectNodeByBorder(b);
                e.Handled = true;
            };

            return b;
        }

        private void Border_PreviewMouseLeftButtonDown(object sender, MouseButtonEventArgs e)
        {
            if (e.ClickCount != 2)
            {
                return;
            }

            e.Handled = true;
            Border_MouseDoubleClick(sender, e);
        }

        private void Border_MouseDoubleClick(object sender, MouseButtonEventArgs _)
        {
            if (sender is not Border border)
            {
                return;
            }

            var node = FindNode(border);
            if (node == null || node.IsRoot)
            {
                return;
            }

            ShowNodeConfigurationDialog(node);
        }


        private void AttachNodeContextMenu(NodeVisual node)
        {
            var menu = new ContextMenu();

            var configItem = new MenuItem
            {
                Header = "Configure",
                IsEnabled = !node.IsRoot
            };
            configItem.Click += (_, _) => ShowNodeConfigurationDialog(node);
            menu.Items.Add(configItem);

            var deleteItem = new MenuItem
            {
                Header = "Delete",
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
            }

            return false;
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
            else
            {
                from.Next = to;
                from.NextPortType = type;
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

            Canvas.SetLeft(n.InputPort, l - PortOffsetX);
            Canvas.SetTop(n.InputPort, t + b.Height / 2 - HalfPortSize);

            if (n.OutputPort != null)
            {
                Canvas.SetLeft(n.OutputPort, l + b.Width - PortOffsetX);
                Canvas.SetTop(n.OutputPort, t + b.Height / 2 - HalfPortSize);
            }

            if (n.OutputPortTrue != null)
            {
                Canvas.SetLeft(n.OutputPortTrue, l + b.Width - PortOffsetX);
                Canvas.SetTop(n.OutputPortTrue, t + b.Height / 3 - HalfPortSize);
            }

            if (n.OutputPortFalse != null)
            {
                Canvas.SetLeft(n.OutputPortFalse, l + b.Width - PortOffsetX);
                Canvas.SetTop(n.OutputPortFalse, t + b.Height * 2 / 3 - HalfPortSize);
            }

            RedrawConnections();
        }

        private void SelectNodeByBorder(Border b)
        {
            var found = allNodes.FirstOrDefault(n => ReferenceEquals(n.Border, b));
            if (found != null)
            {
                SelectNode(found);
            }
        }

        private void SelectNode(NodeVisual node)
        {
            var previous = selectedNode;
            if (ReferenceEquals(previous, node))
            {
                return;
            }

            selectedNode = node;
            if (previous != null)
            {
                Unhighlight(previous);
            }

            Highlight(node);
        }

        private static void Highlight(NodeVisual node)
        {
            node.Border.SetValue(Border.BorderBrushProperty, Brushes.Yellow);
            node.Border.BorderThickness = new Thickness(4);
        }

        private static void Unhighlight(NodeVisual node)
        {
            node.Border.BorderBrush = Brushes.White;
            node.Border.BorderThickness = new Thickness(2);
        }

        private void RedrawConnections()
        {
            foreach (var l in GraphCanvas.Children.OfType<Line>().Where(l => l != tempConnectionLine).ToList())
            {
                GraphCanvas.Children.Remove(l);
            }

            foreach (var n in allNodes)
            {
                if (n.Next != null)
                {
                    var p = GetOutputPortByType(n, n.NextPortType);
                    if (p != null)
                    {
                        DrawLine(n, n.Next, p);
                    }
                }

                if (n.TrueChild != null)
                {
                    var p = GetOutputPortByType(n, n.TruePortType);
                    if (p != null)
                    {
                        DrawLine(n, n.TrueChild, p);
                    }
                }

                if (n.FalseChild != null)
                {
                    var p = GetOutputPortByType(n, n.FalsePortType);
                    if (p != null)
                    {
                        DrawLine(n, n.FalseChild, p);
                    }
                }
            }
        }

        private FrameworkElement? GetOutputPortByType(NodeVisual n, string? type)
        {
            if (type == null)
            {
                return null;
            }

            return type switch
            {
                "true" or "continue" => n.OutputPortTrue,
                "false" or "exit" => n.OutputPortFalse,
                _ => n.OutputPort
            };
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
                if (isConnecting)
                {
                    return;
                }

                isDragging = true;
                dragStart = ev.GetPosition(GraphCanvas);
                e.CaptureMouse();
            };

            e.MouseMove += (_, ev) =>
            {
                if (!isDragging)
                {
                    return;
                }

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

            NodeCategory category;
            if (t.Name.Contains("Condition"))
            {
                category = NodeCategory.Condition;
            }
            else if (t.Name.Contains("Loop") ||
                     t.Name.Contains("Repeat") ||
                     t == typeof(WaitForPacketNode) ||
                     t == typeof(RetryNode) ||
                     t == typeof(RandomChoiceNode))
            {
                category = NodeCategory.Loop;
            }
            else
            {
                category = NodeCategory.Action;
            }

            var b = CreateNodeVisual(t.Name, GetNodeColor(category));
            var n = new NodeVisual
            {
                Border = b,
                Category = category,
                InputPort = CreateInputPort(),
                NodeType = t,
                Configuration = new NodeConfiguration()
            };

            if (category == NodeCategory.Action)
            {
                n.OutputPort = CreateOutputPort("default");
            }
            else
            {
                n.OutputPortTrue = CreateOutputPort(category == NodeCategory.Condition ? "true" : "continue");
                n.OutputPortFalse = CreateOutputPort(category == NodeCategory.Condition ? "false" : "exit");
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
            if (n != selectedNode)
            {
                n.Border.BorderBrush = GetRuntimeBrush(n.RuntimeState);
            }
        }

        private void BuildGraph_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                BuiltGraph = BuildActionGraph();
                Log($"Graph built successfully with {BuiltGraph.GetAllNodes().Count} nodes");

                if (FindName("StatusText") is TextBlock statusText)
                {
                    statusText.Text = $"Graph Built ({BuiltGraph.GetAllNodes().Count} nodes)";
                    statusText.Foreground = Brushes.LightGreen;
                }

                MessageBox.Show($"Graph built successfully!\n{BuiltGraph.GetAllNodes().Count} nodes created.",
                    "Success", MessageBoxButton.OK, MessageBoxImage.Information);
            }
            catch (Exception ex)
            {
                Log($"Error building graph: {ex.Message}");

                if (FindName("StatusText") is TextBlock statusText)
                {
                    statusText.Text = "Build Failed";
                    statusText.Foreground = Brushes.IndianRed;
                }

                MessageBox.Show($"Error building graph: {ex.Message}",
                    "Error", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }
        
        private void ApplyGraph_Click(object sender, RoutedEventArgs e)
        {
            if (BuiltGraph == null)
            {
                MessageBox.Show("Please build the graph first!", "Warning",
                    MessageBoxButton.OK, MessageBoxImage.Warning);

                if (FindName("StatusText") is not TextBlock statusText)
                {
                    return;
                }

                statusText.Text = "Build graph first";
                statusText.Foreground = Brushes.Orange;
                return;
            }

            BotTesterCore.Instance.SetBotActionGraph(BuiltGraph);
            BotTesterCore.Instance.SaveGraphVisuals([..allNodes]);
            Log("Graph applied to BotTesterCore and saved");

            if (FindName("StatusText") is TextBlock statusText2)
            {
                statusText2.Text = "Applied to BotTester";
                statusText2.Foreground = Brushes.LightBlue;
            }

            MessageBox.Show("Graph has been applied successfully!\nYou can now start the bot test.", "Success",
                MessageBoxButton.OK, MessageBoxImage.Information);
        }

        private void Log(string msg)
        {
            LogListBox.Items.Add($"[{DateTime.Now:HH:mm:ss}] {msg}");
            LogListBox.ScrollIntoView(LogListBox.Items[^1]);
        }

        protected override void OnClosing(System.ComponentModel.CancelEventArgs e)
        {
            base.OnClosing(e);
            PreviewKeyDown -= BotActionGraphWindow_PreviewKeyDown;
            
            foreach (var node in allNodes)
            {
                if (node.Border.ContextMenu == null)
                {
                    continue;
                }

                node.Border.ContextMenu.Items.Clear();
                node.Border.ContextMenu = null;
            }
            
            allNodes.Clear();
            GraphCanvas.Children.Clear();
        }

        private static bool EvaluateConditionWithAccessors(RuntimeContext ctx, string? leftType, string left, string op, string? rightType, string right)
        {
            try
            {
                object leftValue;
                if (leftType == "Getter Function")
                {
                    leftValue = VariableAccessorRegistry.InvokeGetter(left, ctx) ?? 0;
                }
                else
                {
                    leftValue = ParseConstant(left);
                }

                object rightValue;
                if (rightType == "Getter Function")
                {
                    rightValue = VariableAccessorRegistry.InvokeGetter(right, ctx) ?? 0;
                }
                else
                {
                    rightValue = ParseConstant(right);
                }

                if (TryConvertToNumber(leftValue, out var lNum) && TryConvertToNumber(rightValue, out var rNum))
                {
                    return op switch
                    {
                        ">" => lNum > rNum,
                        "<" => lNum < rNum,
                        "==" => Math.Abs(lNum - rNum) < 0.0001,
                        ">=" => lNum >= rNum,
                        "<=" => lNum <= rNum,
                        "!=" => Math.Abs(lNum - rNum) >= 0.0001,
                        _ => false
                    };
                }

                return op switch
                {
                    "==" => Equals(leftValue, rightValue),
                    "!=" => !Equals(leftValue, rightValue),
                    _ => false
                };
            }
            catch (Exception ex)
            {
                Serilog.Log.Error($"Condition evaluation failed: {ex.Message}");
                return false;
            }
        }

        private static object ParseConstant(string value)
        {
            if (int.TryParse(value, out var intVal))
            {
                return intVal;
            }

            if (double.TryParse(value, out var doubleVal))
            {
                return doubleVal;
            }

            if (bool.TryParse(value, out var boolVal))
            {
                return boolVal;
            }

            return value;
        }

        private static bool TryConvertToNumber(object value, out double result)
        {
            result = 0;

            if (value is int i)
            {
                result = i;
                return true;
            }

            if (value is double d)
            {
                result = d;
                return true;
            }

            if (value is float f)
            {
                result = f;
                return true;
            }

            if (value is long l)
            {
                result = l;
                return true;
            }

            if (value is uint ui)
            {
                result = ui;
                return true;
            }

            if (value is short s)
            {
                result = s;
                return true;
            }

            if (value is byte b)
            {
                result = b;
                return true;
            }

            if (value is string str && double.TryParse(str, out var parsed))
            {
                result = parsed;
                return true;
            }

            return false;
        }
    }
}
