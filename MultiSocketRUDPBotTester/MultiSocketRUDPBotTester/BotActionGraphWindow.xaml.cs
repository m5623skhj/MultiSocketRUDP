using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Shapes;
using MultiSocketRUDPBotTester.Bot;
using MultiSocketRUDPBotTester.ClientCore;
using MultiSocketRUDPBotTester.Evaluation;
using MultiSocketRUDPBotTester.Windows;
using MultiSocketRUDPBotTester.CanvasRenderer;
using WpfCanvas = System.Windows.Controls.Canvas;
using Microsoft.Extensions.Configuration;
using Path = System.IO.Path;

namespace MultiSocketRUDPBotTester
{
    public partial class BotActionGraphWindow : Window
    {
        private readonly List<NodeVisual> allNodes = [];

        private NodeCanvasRenderer renderer = null!;
        private NodeInteractionHandler interaction = null!;

        private NodeVisual? selectedNode;
        private const double PortOffsetX = 18.0;
        private const double HalfPortSize = 18.0;

        public ActionGraph? BuiltGraph { get; private set; }

        private readonly NodeStatsTracker statsTracker = new();
        private Window? statsWindow;

        private WithGeminiClient.GeminiClient geminiClient = null!;

        public BotActionGraphWindow()
        {
            InitializeComponent();
            ActionNodeBase.SetStatsTracker(statsTracker);
            LoadActionNodeTypes();

            Loaded += OnWindowLoaded;
            PreviewKeyDown += BotActionGraphWindow_PreviewKeyDown;
        }

        private void OnWindowLoaded(object sender, RoutedEventArgs e)
        {
            renderer = new NodeCanvasRenderer(GraphCanvas, allNodes);
            interaction = new NodeInteractionHandler(
                GraphCanvas, GraphScroll, allNodes, renderer, Log);

            GraphCanvas.MouseLeftButtonDown += (_, _) =>
            {
                if (selectedNode == null) return;
                NodeCanvasRenderer.Unhighlight(selectedNode);
                selectedNode = null;
            };

            InitializeGeminiClient();

            var savedVisuals = BotTesterCore.Instance.GetSavedGraphVisuals();
            if (savedVisuals is { Count: > 0 })
                RestoreSavedGraph(savedVisuals);
            else
                CreateRootNode();
        }

        private void InitializeGeminiClient()
        {
            try
            {
                var baseDirectory = AppDomain.CurrentDomain.BaseDirectory;
                const string configFileName = "GeminiClientConfiguration.json";

                var configPath = Path.Combine(baseDirectory, configFileName);
                if (!File.Exists(configPath))
                    configPath = Path.Combine(baseDirectory, "WithGeminiClient", configFileName);

                if (!File.Exists(configPath))
                    throw new FileNotFoundException(
                        $"Configuration file not found in:\n" +
                        $"1. {Path.Combine(baseDirectory, configFileName)}\n" +
                        $"2. {Path.Combine(baseDirectory, "WithGeminiClient", configFileName)}");

                var geminiApiConfig = new ConfigurationBuilder()
                    .SetBasePath(Path.GetDirectoryName(configPath)!)
                    .AddJsonFile(Path.GetFileName(configPath), optional: false, reloadOnChange: false)
                    .Build();

                geminiClient = new WithGeminiClient.GeminiClient(geminiApiConfig);
                Log("GeminiClient initialized successfully");
            }
            catch (Exception ex)
            {
                Log($"Failed to initialize GeminiClient: {ex.Message}");
                MessageBox.Show(
                    $"Failed to initialize AI features:\n{ex.Message}\n\n" +
                    $"AI Tree Generator will not be available.\n\n" +
                    $"Current directory: {Directory.GetCurrentDirectory()}\n" +
                    $"Base directory: {AppDomain.CurrentDomain.BaseDirectory}",
                    "Warning", MessageBoxButton.OK, MessageBoxImage.Warning);
                geminiClient = null!;
            }
        }

        private void LoadActionNodeTypes()
        {
            var baseType = typeof(ActionNodeBase);
            ActionNodeListBox.ItemsSource = baseType.Assembly.GetTypes()
                .Where(t => t is { IsClass: true, IsAbstract: false }
                            && baseType.IsAssignableFrom(t))
                .ToList();
        }

        private void CreateRootNode()
        {
            var b = CreateNodeVisual("OnConnected", Brushes.DarkGreen);
            var n = new NodeVisual
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

            WpfCanvas.SetLeft(b, 50);
            WpfCanvas.SetTop(b, 200);
            AddNodeToCanvas(n);
            Log("Root node created.");
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

            interaction.EnableDrag(b);

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
            if (e.ClickCount != 2) return;
            e.Handled = true;
            if (sender is Border border)
                ShowNodeConfigurationDialog(FindNode(border)!);
        }

        private void AddNodeToCanvas(NodeVisual node)
        {
            AttachNodeContextMenu(node);

            GraphCanvas.Children.Add(node.Border);
            GraphCanvas.Children.Add(node.InputPort);
            if (node.OutputPort != null) GraphCanvas.Children.Add(node.OutputPort);
            if (node.OutputPortTrue != null) GraphCanvas.Children.Add(node.OutputPortTrue);
            if (node.OutputPortFalse != null) GraphCanvas.Children.Add(node.OutputPortFalse);

            allNodes.Add(node);
            renderer.UpdatePortPositions(node);
        }

        private void RedrawConnections() => renderer.RedrawConnections();

        private void AttachNodeContextMenu(NodeVisual node)
        {
            var menu = new ContextMenu();

            var configItem = new MenuItem { Header = "Configure", IsEnabled = !node.IsRoot };
            configItem.Click += (_, _) => ShowNodeConfigurationDialog(node);
            menu.Items.Add(configItem);

            var deleteItem = new MenuItem { Header = "Delete", IsEnabled = !node.IsRoot };
            deleteItem.Click += (_, _) => { if (!node.IsRoot) DeleteNode(node); };
            menu.Items.Add(deleteItem);

            node.Border.ContextMenu = menu;
        }

        private FrameworkElement CreateInputPort()
        {
            var hit = new Grid
            {
                Width = 40,
                Height = 40,
                Background = Brushes.Transparent,
                Tag = "input"
            };
            var circle = new Ellipse
            {
                Width = 24,
                Height = 24,
                Fill = Brushes.LightGreen,
                Stroke = Brushes.White,
                StrokeThickness = 3,
                IsHitTestVisible = false
            };
            hit.Children.Add(circle);
            Panel.SetZIndex(hit, 1000);
            hit.MouseLeftButtonUp += InputPort_MouseUp;
            return hit;
        }

        private FrameworkElement CreateOutputPort(string type)
        {
            var hit = new Grid
            {
                Width = 40,
                Height = 40,
                Background = Brushes.Transparent,
                Tag = type
            };
            var circle = new Ellipse
            {
                Width = 24,
                Height = 24,
                Fill = NodeInteractionHandler.GetPortColor(type),
                Stroke = Brushes.White,
                StrokeThickness = 3,
                IsHitTestVisible = false
            };
            hit.Children.Add(circle);
            Panel.SetZIndex(hit, 1000);
            hit.MouseLeftButtonDown += (_, _) => interaction.StartConnection(hit, type);
            return hit;
        }

        private void InputPort_MouseUp(object sender, MouseButtonEventArgs e)
        {
            e.Handled = true;
            if (sender is FrameworkElement port)
                interaction.TryFinishConnection(port);
        }

        private void AddNode_Click(object sender, RoutedEventArgs e)
        {
            if (ActionNodeListBox.SelectedItem is not Type t) return;

            var category = t.Name switch
            {
                _ when t.Name.Contains("Condition") => NodeCategory.Condition,
                _ when t.Name.Contains("Loop")
                       || t.Name.Contains("Repeat")
                       || t == typeof(WaitForPacketNode)
                       || t == typeof(RetryNode)
                       || t == typeof(RandomChoiceNode) => NodeCategory.Loop,
                _ => NodeCategory.Action
            };

            var b = CreateNodeVisual(t.Name, GetNodeColor(category));
            var n = new NodeVisual
            {
                Border = b,
                Category = category,
                InputPort = CreateInputPort(),
                NodeType = t,
                Configuration = new NodeConfiguration()
            };

            if (t == typeof(RandomChoiceNode))
            {
                n.Configuration.IntValue = 2;
                CreateDynamicPorts(n, 2);
            }
            else if (category == NodeCategory.Action)
            {
                n.OutputPort = CreateOutputPort("default");
            }
            else
            {
                n.OutputPortTrue = CreateOutputPort(category == NodeCategory.Condition ? "true" : "continue");
                n.OutputPortFalse = CreateOutputPort(category == NodeCategory.Condition ? "false" : "exit");
            }

            WpfCanvas.SetLeft(b, GraphScroll.HorizontalOffset + 400);
            WpfCanvas.SetTop(b, GraphScroll.VerticalOffset + 300);
            AddNodeToCanvas(n);
            Log($"Node added: {t.Name}");
        }

        private void CreateDynamicPorts(NodeVisual node, int count)
        {
            foreach (var port in node.DynamicOutputPorts)
                GraphCanvas.Children.Remove(port);

            node.DynamicOutputPorts.Clear();
            node.DynamicPortTypes.Clear();
            node.DynamicChildren.Clear();

            for (var i = 0; i < count; i++)
            {
                var portType = $"choice_{i}";
                var port = CreateOutputPort(portType);
                node.DynamicOutputPorts.Add(port);
                node.DynamicPortTypes.Add(portType);
                node.DynamicChildren.Add(null);
                GraphCanvas.Children.Add(port);
            }

            renderer.UpdatePortPositions(node);
        }

        private void SelectNodeByBorder(Border b)
        {
            var found = allNodes.FirstOrDefault(n => ReferenceEquals(n.Border, b));
            if (found != null) SelectNode(found);
        }

        private void SelectNode(NodeVisual node)
        {
            if (ReferenceEquals(selectedNode, node)) return;
            if (selectedNode != null) NodeCanvasRenderer.Unhighlight(selectedNode);
            selectedNode = node;
            NodeCanvasRenderer.Highlight(node);
        }

        private void DeleteNode(NodeVisual node)
        {
            if (node.IsRoot) { Log("Root node cannot be deleted."); return; }

            DisconnectIncoming(node);

            GraphCanvas.Children.Remove(node.Border);
            GraphCanvas.Children.Remove(node.InputPort);
            if (node.OutputPort != null) GraphCanvas.Children.Remove(node.OutputPort);
            if (node.OutputPortTrue != null) GraphCanvas.Children.Remove(node.OutputPortTrue);
            if (node.OutputPortFalse != null) GraphCanvas.Children.Remove(node.OutputPortFalse);
            foreach (var port in node.DynamicOutputPorts)
                GraphCanvas.Children.Remove(port);

            allNodes.Remove(node);
            renderer.RedrawConnections();
            Log($"Node deleted: {((TextBlock)node.Border.Child).Text}");
        }

        private void DisconnectIncoming(NodeVisual node)
        {
            foreach (var n in allNodes)
            {
                if (n.Next == node) n.Next = null;
                if (n.TrueChild == node) n.TrueChild = null;
                if (n.FalseChild == node) n.FalseChild = null;
            }
        }

        private NodeVisual? FindNode(Border b) =>
            allNodes.FirstOrDefault(n => n.Border == b);

        private void RestoreSavedGraph(List<NodeVisual> savedVisuals)
        {
            var nodeMapping = new Dictionary<NodeVisual, NodeVisual>();
            var positionMapping = new Dictionary<NodeVisual, (double left, double top)>();

            foreach (var saved in savedVisuals)
                positionMapping[saved] = (WpfCanvas.GetLeft(saved.Border),
                                          WpfCanvas.GetTop(saved.Border));

            foreach (var saved in savedVisuals)
            {
                var newNode = CloneNodeVisual(saved, positionMapping[saved]);
                nodeMapping[saved] = newNode;

                AttachNodeContextMenu(newNode);
                GraphCanvas.Children.Add(newNode.Border);
                GraphCanvas.Children.Add(newNode.InputPort);
                if (newNode.OutputPort != null) GraphCanvas.Children.Add(newNode.OutputPort);
                if (newNode.OutputPortTrue != null) GraphCanvas.Children.Add(newNode.OutputPortTrue);
                if (newNode.OutputPortFalse != null) GraphCanvas.Children.Add(newNode.OutputPortFalse);

                allNodes.Add(newNode);

                var l = WpfCanvas.GetLeft(newNode.Border);
                var t = WpfCanvas.GetTop(newNode.Border);
                var h = newNode.Border.Height;
                var w = newNode.Border.Width;

                WpfCanvas.SetLeft(newNode.InputPort, l - PortOffsetX);
                WpfCanvas.SetTop(newNode.InputPort, t + h / 2 - HalfPortSize);

                if (newNode.OutputPort != null)
                {
                    WpfCanvas.SetLeft(newNode.OutputPort, l + w - PortOffsetX);
                    WpfCanvas.SetTop(newNode.OutputPort, t + h / 2 - HalfPortSize);
                }
                if (newNode.OutputPortTrue != null)
                {
                    WpfCanvas.SetLeft(newNode.OutputPortTrue, l + w - PortOffsetX);
                    WpfCanvas.SetTop(newNode.OutputPortTrue, t + h / 3 - HalfPortSize);
                }
                if (newNode.OutputPortFalse != null)
                {
                    WpfCanvas.SetLeft(newNode.OutputPortFalse, l + w - PortOffsetX);
                    WpfCanvas.SetTop(newNode.OutputPortFalse, t + h * 2 / 3 - HalfPortSize);
                }
            }

            foreach (var saved in savedVisuals)
            {
                if (!nodeMapping.TryGetValue(saved, out var newNode)) continue;

                if (saved.Next != null && nodeMapping.TryGetValue(saved.Next, out var next))
                    newNode.Next = next;
                if (saved.TrueChild != null && nodeMapping.TryGetValue(saved.TrueChild, out var trueN))
                    newNode.TrueChild = trueN;
                if (saved.FalseChild != null && nodeMapping.TryGetValue(saved.FalseChild, out var falseN))
                    newNode.FalseChild = falseN;
            }

            Dispatcher.BeginInvoke(
                System.Windows.Threading.DispatcherPriority.Loaded,
                new Action(() => renderer.RedrawConnections()));
            Log($"Graph restored with {allNodes.Count} nodes.");
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
                newNode.OutputPort = CreateOutputPort("default");
            else
            {
                newNode.OutputPortTrue = CreateOutputPort(
                    original.Category == NodeCategory.Condition ? "true" : "continue");
                newNode.OutputPortFalse = CreateOutputPort(
                    original.Category == NodeCategory.Condition ? "false" : "exit");
            }

            WpfCanvas.SetLeft(border, position.left);
            WpfCanvas.SetTop(border, position.top);
            return newNode;
        }

        private static NodeConfiguration? CloneConfiguration(NodeConfiguration? original)
        {
            if (original == null) return null;
            return new NodeConfiguration
            {
                PacketId = original.PacketId,
                StringValue = original.StringValue,
                IntValue = original.IntValue,
                Properties = new Dictionary<string, object>(original.Properties)
            };
        }

        private void BotActionGraphWindow_PreviewKeyDown(object sender, KeyEventArgs e)
        {
            if (e.Key != Key.Delete || selectedNode == null) return;
            DeleteNode(selectedNode);
            selectedNode = null;
        }

        private void ShowStatsWindow_Click(object sender, RoutedEventArgs e)
        {
            if (statsWindow?.IsVisible == true) { statsWindow.Activate(); return; }
            statsWindow = StatsWindowBuilder.Build(this, statsTracker);
            statsWindow.Closed += (_, _) => statsWindow = null;
            statsWindow.Show();
        }

        private void ValidateGraph_Click(object sender, RoutedEventArgs e)
        {
            if (BuiltGraph == null)
            {
                MessageBox.Show("Please build the graph first!", "Warning",
                    MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }
            ValidationWindowBuilder.ShowDialog(this, GraphValidator.ValidateGraph(BuiltGraph));
        }

        private void ShowValidationWindow(GraphValidationResult result) =>
            ValidationWindowBuilder.ShowDialog(this, result);

        private void BuildGraph_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                BuiltGraph = BuildActionGraph();
                var validation = GraphValidator.ValidateGraph(BuiltGraph);

                if (!validation.IsValid)
                {
                    ShowValidationWindow(validation);
                    MessageBox.Show($"{validation.ErrorCount} errors found. Please fix them.",
                        "Validation Failed", MessageBoxButton.OK, MessageBoxImage.Error);
                    return;
                }

                if (validation.WarningCount > 0 &&
                    MessageBox.Show($"{validation.WarningCount} warnings. Continue?",
                        "Warnings", MessageBoxButton.YesNo) == MessageBoxResult.No)
                {
                    ShowValidationWindow(validation);
                    return;
                }

                Log($"Graph built successfully with {BuiltGraph.GetAllNodes().Count} nodes");
                SetStatusText($"Graph Built ({BuiltGraph.GetAllNodes().Count} nodes)", Brushes.LightGreen);
                MessageBox.Show($"Graph built successfully!\n{BuiltGraph.GetAllNodes().Count} nodes created.",
                    "Success", MessageBoxButton.OK, MessageBoxImage.Information);
            }
            catch (Exception ex)
            {
                Log($"Error building graph: {ex.Message}");
                SetStatusText("Build Failed", Brushes.IndianRed);
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
                SetStatusText("Build graph first", Brushes.Orange);
                return;
            }

            BotTesterCore.Instance.SetBotActionGraph(BuiltGraph);
            BotTesterCore.Instance.SaveGraphVisuals([.. allNodes]);
            Log("Graph applied to BotTesterCore and saved");
            SetStatusText("Applied to BotTester", Brushes.LightBlue);
            MessageBox.Show("Graph has been applied successfully!\nYou can now start the bot test.",
                "Success", MessageBoxButton.OK, MessageBoxImage.Information);
        }

        private void SetStatusText(string text, Brush color)
        {
            if (FindName("StatusText") is not TextBlock tb) return;
            tb.Text = text;
            tb.Foreground = color;
        }

        private static bool EvaluateConditionWithAccessors(
            RuntimeContext ctx,
            string? leftType, string left,
            string op,
            string? rightType, string right) =>
            ConditionEvaluator.Evaluate(ctx, leftType, left, op, rightType, right);

        protected override void OnClosing(System.ComponentModel.CancelEventArgs e)
        {
            base.OnClosing(e);
            PreviewKeyDown -= BotActionGraphWindow_PreviewKeyDown;

            foreach (var node in allNodes)
            {
                if (node.Border.ContextMenu == null) continue;
                node.Border.ContextMenu.Items.Clear();
                node.Border.ContextMenu = null;
            }

            allNodes.Clear();
            GraphCanvas.Children.Clear();
        }

        private void Log(string msg)
        {
            LogListBox.Items.Add($"[{DateTime.Now:HH:mm:ss}] {msg}");
            LogListBox.ScrollIntoView(LogListBox.Items[^1]);
        }

        public enum NodeRuntimeState { Idle, Running, Success, Fail }
        public enum NodeCategory { Action, Condition, Loop }

        private static Brush GetNodeColor(NodeCategory c) => c switch
        {
            NodeCategory.Condition => Brushes.DarkOrange,
            NodeCategory.Loop => Brushes.DarkMagenta,
            _ => Brushes.DimGray
        };
    }
}