using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Shapes;
using MultiSocketRUDPBotTester.Bot;

namespace MultiSocketRUDPBotTester
{
    /// <summary>
    /// BotActionGraphWindow.xaml에 대한 상호 작용 논리
    /// </summary>
    public partial class BotActionGraphWindow : Window
    {
        private readonly List<NodeVisual> allNodes = [];
        private NodeVisual? rootNode;
        private Point dragStart;
        private bool isDragging;
        private NodeVisual? selectedNode;
        private bool isConnecting;
        private Line? tempConnectionLine;
        private Ellipse? connectingFromPort;
        private string? connectingPortType;

        public BotActionGraphWindow()
        {
            InitializeComponent();
            LoadActionNodeTypes();
            CreateRootNode();
        }

        private void CreateRootNode()
        {
            var rootBorder = CreateNodeVisual("OnConnected", Brushes.DarkGreen);
            rootNode = new NodeVisual
            {
                Border = rootBorder,
                NodeType = typeof(CustomActionNode),
                Category = NodeCategory.Action,
                OutputPort = CreateOutputPort(rootBorder, "default"),
                InputPort = CreateInputPort(rootBorder)
            };

            Canvas.SetLeft(rootBorder, 50);
            Canvas.SetTop(rootBorder, 200);

            GraphCanvas.Children.Add(rootBorder);
            GraphCanvas.Children.Add(rootNode.OutputPort);
            GraphCanvas.Children.Add(rootNode.InputPort);
            allNodes.Add(rootNode);

            UpdatePortPositions(rootNode);
        }

        private void LoadActionNodeTypes()
        {
            var baseType = typeof(ActionNodeBase);
            var assembly = baseType.Assembly;

            var types = assembly.GetTypes()
                .Where(t => t is { IsClass: true, IsAbstract: false }
                            && baseType.IsAssignableFrom(t))
                .ToList();

            ActionNodeListBox.ItemsSource = types;
        }

        private Border CreateNodeVisual(string title, Brush background)
        {
            var textBlock = new TextBlock
            {
                Text = title,
                Foreground = Brushes.White,
                HorizontalAlignment = HorizontalAlignment.Center,
                VerticalAlignment = VerticalAlignment.Center,
                TextWrapping = TextWrapping.Wrap,
                Margin = new Thickness(10, 5, 10, 5)
            };

            var border = new Border
            {
                Width = 140,
                Height = 60,
                Background = background,
                BorderBrush = Brushes.White,
                BorderThickness = new Thickness(2),
                CornerRadius = new CornerRadius(4),
                Child = textBlock
            };

            EnableDrag(border);
            return border;
        }
        
        private static Brush GetPortColor(string portType)
        {
            return portType switch
            {
                "true" or "continue" => Brushes.LightGreen,
                "false" or "exit" => Brushes.IndianRed,
                _ => Brushes.LightBlue
            };
        }

        private Ellipse CreateInputPort(Border _)
        {
            var port = new Ellipse
            {
                Width = 20,
                Height = 20,
                Fill = Brushes.LightGreen,
                Stroke = Brushes.White,
                StrokeThickness = 4,
                Cursor = Cursors.Hand,
                Tag = "input"
            };

            port.MouseEnter += (s, e) =>
            {
                if (isConnecting)
                {
                    port.Fill = Brushes.Yellow;
                }
            };
            port.MouseLeave += (s, e) =>
            {
                port.Fill = Brushes.LightGreen;
            };

            port.MouseLeftButtonUp += InputPort_MouseUp;

            return port;
        }

        private Ellipse CreateOutputPort(Border _, string portType)
        {
            var port = new Ellipse
            {
                Width = 20,
                Height = 20,
                Fill = GetPortColor(portType),
                Stroke = Brushes.White,
                StrokeThickness = 4,
                Cursor = Cursors.Hand,
                Tag = portType
            };

            port.MouseLeftButtonDown += (s, e) => OutputPort_MouseDown(s, e, portType);
            port.MouseLeftButtonUp += OutputPort_MouseUp;
            port.MouseEnter += (_, _) => port.Fill = Brushes.Yellow;
            port.MouseLeave += (_, _) => port.Fill = GetPortColor(portType);

            return port;
        }

        private void InputPort_MouseUp(object sender, MouseButtonEventArgs e)
        {
            if (!isConnecting || selectedNode == null)
            {
                return;
            }

            var inputPort = sender as Ellipse;
            var targetNode = allNodes.FirstOrDefault(n => n.InputPort == inputPort);

            if (targetNode != null && targetNode != selectedNode)
            {
                ConnectNodes(selectedNode, targetNode, connectingPortType ?? "default");
            }

            isConnecting = false;
            if (tempConnectionLine != null)
            {
                GraphCanvas.Children.Remove(tempConnectionLine);
                tempConnectionLine = null;
            }

            e.Handled = true;
        }

        private void UpdatePortPositions(NodeVisual node)
        {
            if (node.OutputPort != null)
            {
                var outputCenter = node.GetOutputPortCenter();
                Canvas.SetLeft(node.OutputPort, outputCenter.X - node.OutputPort.Width / 2);
                Canvas.SetTop(node.OutputPort, outputCenter.Y - node.OutputPort.Height / 2);
            }

            if (node.OutputPortTrue != null)
            {
                var outputCenter = node.GetOutputPortTrueCenter();
                Canvas.SetLeft(node.OutputPortTrue, outputCenter.X - node.OutputPortTrue.Width / 2);
                Canvas.SetTop(node.OutputPortTrue, outputCenter.Y - node.OutputPortTrue.Height / 2);
            }

            if (node.OutputPortFalse != null)
            {
                var outputCenter = node.GetOutputPortFalseCenter();
                Canvas.SetLeft(node.OutputPortFalse, outputCenter.X - node.OutputPortFalse.Width / 2);
                Canvas.SetTop(node.OutputPortFalse, outputCenter.Y - node.OutputPortFalse.Height / 2);
            }

            var inputCenter = node.GetInputPortCenter();
            Canvas.SetLeft(node.InputPort, inputCenter.X - node.InputPort.Width / 2);
            Canvas.SetTop(node.InputPort, inputCenter.Y - node.InputPort.Height / 2);

            UpdateConnectionLines(node);
        }

        private void UpdateConnectionLines(NodeVisual node)
        {
            foreach (var line in node.OutputLines)
            {
                var outputCenter = node.GetOutputPortCenter();
                line.X1 = outputCenter.X;
                line.Y1 = outputCenter.Y;

                var child = node.Children.FirstOrDefault();
                if (child == null)
                {
                    continue;
                }

                var inputCenter = child.GetInputPortCenter();
                line.X2 = inputCenter.X;
                line.Y2 = inputCenter.Y;
            }

            foreach (var line in node.OutputLinesTrue)
            {
                var outputCenter = node.GetOutputPortTrueCenter();
                line.X1 = outputCenter.X;
                line.Y1 = outputCenter.Y;

                var child = node.ChildrenTrue.FirstOrDefault();
                if (child == null)
                {
                    continue;
                }

                var inputCenter = child.GetInputPortCenter();
                line.X2 = inputCenter.X;
                line.Y2 = inputCenter.Y;
            }

            foreach (var line in node.OutputLinesFalse)
            {
                var outputCenter = node.GetOutputPortFalseCenter();
                line.X1 = outputCenter.X;
                line.Y1 = outputCenter.Y;

                var child = node.ChildrenFalse.FirstOrDefault();
                if (child == null)
                {
                    continue;
                }

                var inputCenter = child.GetInputPortCenter();
                line.X2 = inputCenter.X;
                line.Y2 = inputCenter.Y;
            }

            if (node.Parent != null)
            {
                foreach (var line in node.Parent.OutputLines)
                {
                    if (!node.Parent.Children.Contains(node))
                    {
                        continue;
                    }

                    var outputCenter = node.Parent.GetOutputPortCenter();
                    var inputCenter = node.GetInputPortCenter();
                    line.X1 = outputCenter.X;
                    line.Y1 = outputCenter.Y;
                    line.X2 = inputCenter.X;
                    line.Y2 = inputCenter.Y;
                }
            }

            var trueParent = allNodes.FirstOrDefault(n => n.ChildrenTrue.Contains(node));
            if (trueParent != null)
            {
                foreach (var line in trueParent.OutputLinesTrue)
                {
                    var outputCenter = trueParent.GetOutputPortTrueCenter();
                    var inputCenter = node.GetInputPortCenter();
                    line.X1 = outputCenter.X;
                    line.Y1 = outputCenter.Y;
                    line.X2 = inputCenter.X;
                    line.Y2 = inputCenter.Y;
                }
            }

            var falseParent = allNodes.FirstOrDefault(n => n.ChildrenFalse.Contains(node));
            if (falseParent == null)
            {
                return;
            }

            foreach (var line in falseParent.OutputLinesFalse)
            {
                var outputCenter = falseParent.GetOutputPortFalseCenter();
                var inputCenter = node.GetInputPortCenter();
                line.X1 = outputCenter.X;
                line.Y1 = outputCenter.Y;
                line.X2 = inputCenter.X;
                line.Y2 = inputCenter.Y;
            }
        }

        private void OutputPort_MouseDown(object sender, MouseButtonEventArgs e, string portType)
        {
            isConnecting = true;
            connectingPortType = portType;
            var port = sender as Ellipse;
            connectingFromPort = port;
            selectedNode = allNodes.FirstOrDefault(n =>
                n.OutputPort == port ||
                n.OutputPortTrue == port ||
                n.OutputPortFalse == port);

            if (selectedNode != null)
            {
                tempConnectionLine = new Line
                {
                    Stroke = GetPortColor(portType),
                    StrokeThickness = 3,
                    StrokeDashArray = [5, 2]
                };

                Point start;
                if (portType is "true" or "continue")
                {
                    start = selectedNode.GetOutputPortTrueCenter();
                }
                else if (portType is "false" or "exit")
                {
                    start = selectedNode.GetOutputPortFalseCenter();
                }
                else
                {
                    start = selectedNode.GetOutputPortCenter();
                }

                tempConnectionLine.X1 = start.X;
                tempConnectionLine.Y1 = start.Y;
                tempConnectionLine.X2 = start.X;
                tempConnectionLine.Y2 = start.Y;

                GraphCanvas.Children.Add(tempConnectionLine);
            }

            e.Handled = true;
        }

        private void OutputPort_MouseUp(object sender, MouseButtonEventArgs e)
        {
            if (isConnecting && tempConnectionLine != null)
            {
                GraphCanvas.Children.Remove(tempConnectionLine);
                tempConnectionLine = null;
            }

            isConnecting = false;
            e.Handled = true;
        }

        private void AddNode_Click(object sender, RoutedEventArgs e)
        {
            if (ActionNodeListBox.SelectedItem is not Type type)
            {
                return;
            }

            var category = NodeCategory.Action;
            var nodeName = type.Name;

            if (nodeName.Contains("Condition") || nodeName.Contains("If"))
            {
                category = NodeCategory.Condition;
            }
            else if (nodeName.Contains("Loop") || nodeName.Contains("Repeat"))
            {
                category = NodeCategory.Loop;
            }

            var nodeBorder = CreateNodeVisual(nodeName, GetNodeColor(category));
            var newNode = new NodeVisual
            {
                Border = nodeBorder,
                NodeType = type,
                Category = category,
                InputPort = CreateInputPort(nodeBorder)
            };

            if (category == NodeCategory.Action)
            {
                newNode.OutputPort = CreateOutputPort(nodeBorder, "default");
            }
            else if (category == NodeCategory.Condition)
            {
                newNode.OutputPortTrue = CreateOutputPort(nodeBorder, "true");
                newNode.OutputPortFalse = CreateOutputPort(nodeBorder, "false");
            }
            else
            {
                newNode.OutputPortTrue = CreateOutputPort(nodeBorder, "continue");
                newNode.OutputPortFalse = CreateOutputPort(nodeBorder, "exit");
            }

            Canvas.SetLeft(nodeBorder, 300);
            Canvas.SetTop(nodeBorder, 200);

            GraphCanvas.Children.Add(nodeBorder);
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

            UpdatePortPositions(newNode);
        }

        private static Brush GetNodeColor(NodeCategory category)
        {
            return category switch
            {
                NodeCategory.Condition => Brushes.DarkOrange,
                NodeCategory.Loop => Brushes.DarkMagenta,
                _ => Brushes.DimGray
            };
        }

        private void ConnectNodes(NodeVisual from, NodeVisual to, string portType)
        {
            if (portType == "default" && to.Parent != null)
            {
                MessageBox.Show("This node already has a parent connection.");
                return;
            }

            var connectionLine = CreateConnectionLine(GetPortColor(portType));
            if (portType is "true" or "continue")
            {
                from.OutputLinesTrue.Add(connectionLine);
                from.ChildrenTrue.Add(to);
            }
            else if (portType is "false" or "exit")
            {
                from.OutputLinesFalse.Add(connectionLine);
                from.ChildrenFalse.Add(to);
            }
            else
            {
                from.OutputLines.Add(connectionLine);
                from.Children.Add(to);
                to.Parent = from;
            }

            GraphCanvas.Children.Add(connectionLine);
            Panel.SetZIndex(connectionLine, -1);

            UpdateConnectionLines(from);
        }

        private static Line CreateConnectionLine(Brush color)
        {
            var line = new Line
            {
                Stroke = color,
                StrokeThickness = 3,
                StrokeEndLineCap = PenLineCap.Triangle,
                StrokeStartLineCap = PenLineCap.Round
            };

            return line;
        }

        private void EnableDrag(UIElement element)
        {
            element.MouseLeftButtonDown += (_, e) =>
            {
                if (isConnecting)
                {
                    return;
                }

                isDragging = true;
                dragStart = e.GetPosition(GraphCanvas);
                element.CaptureMouse();
                e.Handled = true;
            };

            element.MouseMove += (_, e) =>
            {
                if (!isDragging)
                {
                    return;
                }

                var pos = e.GetPosition(GraphCanvas);
                var left = Canvas.GetLeft(element) + (pos.X - dragStart.X);
                var top = Canvas.GetTop(element) + (pos.Y - dragStart.Y);

                Canvas.SetLeft(element, left);
                Canvas.SetTop(element, top);

                dragStart = pos;

                var node = allNodes.FirstOrDefault(n => n.Border == element);
                if (node != null)
                {
                    UpdatePortPositions(node);
                }
            };

            element.MouseLeftButtonUp += (_, _) =>
            {
                isDragging = false;
                element.ReleaseMouseCapture();
            };

            GraphCanvas.MouseMove += (_, e) =>
            {
                if (!isConnecting || tempConnectionLine == null)
                {
                    return;
                }

                var pos = e.GetPosition(GraphCanvas);
                tempConnectionLine.X2 = pos.X;
                tempConnectionLine.Y2 = pos.Y;
            };
        }

        public class NodeVisual
        {
            public Border Border { get; set; } = null!;
            public Type NodeType { get; set; } = null!;
            public NodeCategory Category { get; set; } = NodeCategory.Action;
            public List<NodeVisual> Children { get; set; } = [];
            public NodeVisual? Parent { get; set; }
            public List<Line> OutputLines { get; set; } = [];
            public Ellipse? OutputPort { get; set; }
            public Ellipse InputPort { get; set; } = null!;

            public Ellipse? OutputPortTrue { get; set; }
            public Ellipse? OutputPortFalse { get; set; }
            public List<NodeVisual> ChildrenTrue { get; set; } = [];
            public List<NodeVisual> ChildrenFalse { get; set; } = [];
            public List<Line> OutputLinesTrue { get; set; } = [];
            public List<Line> OutputLinesFalse { get; set; } = [];

            public Point GetInputPortCenter()
            {
                var left = Canvas.GetLeft(Border);
                var top = Canvas.GetTop(Border);
                return new Point(left, top + Border.Height / 2);
            }

            public Point GetOutputPortCenter()
            {
                var left = Canvas.GetLeft(Border);
                var top = Canvas.GetTop(Border);
                return new Point(left + Border.Width, top + Border.Height / 2);
            }

            public Point GetOutputPortTrueCenter()
            {
                var left = Canvas.GetLeft(Border);
                var top = Canvas.GetTop(Border);
                return new Point(left + Border.Width, top + Border.Height / 3);
            }

            public Point GetOutputPortFalseCenter()
            {
                var left = Canvas.GetLeft(Border);
                var top = Canvas.GetTop(Border);
                return new Point(left + Border.Width, top + Border.Height * 2 / 3);
            }
        }

        public enum NodeCategory
        {
            Action,
            Condition,
            Loop,
        }
    }
}