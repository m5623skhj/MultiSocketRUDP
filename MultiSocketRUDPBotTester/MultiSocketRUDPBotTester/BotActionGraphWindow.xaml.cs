using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Shapes;
using MultiSocketRUDPBotTester.Bot;
using MultiSocketRUDPBotTester.ClientCore;
using MultiSocketRUDPBotTester.Evaluation;
using MultiSocketRUDPBotTester.Graph;
using MultiSocketRUDPBotTester.Windows;
using MultiSocketRUDPBotTester.CanvasRenderer;
using WpfCanvas = System.Windows.Controls.Canvas;
using Microsoft.Extensions.Configuration;
using Microsoft.Win32;
using Path = System.IO.Path;
using System.Text.Json;
using System.Text.Json.Serialization;
using static MultiSocketRUDPBotTester.Bot.NodeExecutionStats;

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
        private const int MaxLogItems = 500;

        public ActionGraph? BuiltGraph { get; private set; }

        private readonly NodeStatsTracker statsTracker = new();
        private Window? statsWindow;

        private WithGeminiClient.GeminiClient geminiClient = null!;
        private static readonly JsonSerializerOptions GraphFileJsonOptions = new()
        {
            WriteIndented = true,
            Converters = { new JsonStringEnumConverter() }
        };

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
            {
                RestoreSavedGraph(savedVisuals);
            }
            else
            {
                CreateRootNode();
            }
        }

        private void InitializeGeminiClient()
        {
            try
            {
                var baseDirectory = AppDomain.CurrentDomain.BaseDirectory;
                const string configFileName = "GeminiClientConfiguration.json";

                var configPath = Path.Combine(baseDirectory, configFileName);
                if (!File.Exists(configPath))
                {
                    configPath = Path.Combine(baseDirectory, "WithGeminiClient", configFileName);
                }

                if (!File.Exists(configPath))
                {
                    throw new FileNotFoundException(
                        $"Configuration file not found in:\n" +
                        $"1. {Path.Combine(baseDirectory, configFileName)}\n" +
                        $"2. {Path.Combine(baseDirectory, "WithGeminiClient", configFileName)}");
                }

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
            if (e.ClickCount != 2)
            {
                return;
            }

            e.Handled = true;
            if (sender is Border border)
            {
                ShowNodeConfigurationDialog(FindNode(border)!);
            }
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
            {
                interaction.TryFinishConnection(port);
            }
        }

        private void AddNode_Click(object sender, RoutedEventArgs e)
        {
            if (ActionNodeListBox.SelectedItem is not Type t)
            {
                return;
            }

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

        private void SaveGraph_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                var dialog = new SaveFileDialog
                {
                    Filter = "Bot Graph (*.botgraph.json)|*.botgraph.json|JSON Files (*.json)|*.json",
                    DefaultExt = ".botgraph.json",
                    AddExtension = true,
                    FileName = "BotActionGraph.botgraph.json"
                };

                if (dialog.ShowDialog(this) != true)
                {
                    return;
                }

                var graphFile = CreateGraphFileModel();
                var json = JsonSerializer.Serialize(graphFile, GraphFileJsonOptions);
                File.WriteAllText(dialog.FileName, json);

                Log($"Graph saved: {dialog.FileName}");
                SetStatusText("Graph Saved", Brushes.LightGreen);
            }
            catch (Exception ex)
            {
                Log($"Error saving graph: {ex.Message}");
                SetStatusText("Save Failed", Brushes.IndianRed);
                MessageBox.Show($"Error saving graph: {ex.Message}",
                    "Error", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private void LoadGraph_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                var dialog = new OpenFileDialog
                {
                    Filter = "Bot Graph (*.botgraph.json)|*.botgraph.json|JSON Files (*.json)|*.json",
                    Multiselect = false
                };

                if (dialog.ShowDialog(this) != true)
                {
                    return;
                }

                var json = File.ReadAllText(dialog.FileName);
                var graphFile = JsonSerializer.Deserialize<GraphFileModel>(json, GraphFileJsonOptions);
                if (graphFile == null || graphFile.Nodes.Count == 0)
                {
                    throw new InvalidOperationException("Graph file is empty or invalid.");
                }

                ClearCurrentGraph();
                RestoreGraphFromFile(graphFile);
                BuiltGraph = null;

                Log($"Graph loaded: {dialog.FileName}");
                SetStatusText("Graph Loaded", Brushes.LightBlue);
            }
            catch (Exception ex)
            {
                Log($"Error loading graph: {ex.Message}");
                SetStatusText("Load Failed", Brushes.IndianRed);
                MessageBox.Show($"Error loading graph: {ex.Message}",
                    "Error", MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private void CreateDynamicPorts(NodeVisual node, int count)
        {
            foreach (var port in node.DynamicOutputPorts)
            {
                GraphCanvas.Children.Remove(port);
            }

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
            if (found != null)
            {
                SelectNode(found);
            }
        }

        private void SelectNode(NodeVisual node)
        {
            if (ReferenceEquals(selectedNode, node))
            {
                return;
            }
            if (selectedNode != null)
            {
                NodeCanvasRenderer.Unhighlight(selectedNode);
            }

            selectedNode = node;
            NodeCanvasRenderer.Highlight(node);
        }

        private void DeleteNode(NodeVisual node)
        {
            if (node.IsRoot) 
            {
                Log("Root node cannot be deleted."); return; 
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
            foreach (var port in node.DynamicOutputPorts)
            {
                GraphCanvas.Children.Remove(port);
            }

            allNodes.Remove(node);
            renderer.RedrawConnections();
            Log($"Node deleted: {((TextBlock)node.Border.Child).Text}");
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

                for (var i = 0; i < n.DynamicChildren.Count; i++)
                {
                    if (n.DynamicChildren[i] == node)
                    {
                        n.DynamicChildren[i] = null;
                    }
                }
            }
        }

        private NodeVisual? FindNode(Border b) =>
            allNodes.FirstOrDefault(n => n.Border == b);

        private void RestoreSavedGraph(List<NodeVisual> savedVisuals)
        {
            var nodeMapping = new Dictionary<NodeVisual, NodeVisual>();
            var positionMapping = new Dictionary<NodeVisual, (double left, double top)>();

            foreach (var saved in savedVisuals)
            {
                positionMapping[saved] = (WpfCanvas.GetLeft(saved.Border),
                WpfCanvas.GetTop(saved.Border));
            }

            foreach (var saved in savedVisuals)
            {
                var newNode = CloneNodeVisual(saved, positionMapping[saved]);
                nodeMapping[saved] = newNode;

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

                if (saved.NodeType == typeof(RandomChoiceNode) && saved.DynamicOutputPorts.Count > 0)
                {
                    CreateDynamicPorts(newNode, saved.DynamicOutputPorts.Count);
                }
            }

            foreach (var saved in savedVisuals)
            {
                if (!nodeMapping.TryGetValue(saved, out var newNode))
                {
                    continue;
                }

                if (saved.Next != null && nodeMapping.TryGetValue(saved.Next, out var next))
                {
                    newNode.Next = next;
                }
                if (saved.TrueChild != null && nodeMapping.TryGetValue(saved.TrueChild, out var trueN))
                {
                    newNode.TrueChild = trueN;
                }
                if (saved.FalseChild != null && nodeMapping.TryGetValue(saved.FalseChild, out var falseN))
                {
                    newNode.FalseChild = falseN;
                }

                for (var i = 0; i < saved.DynamicChildren.Count; i++)
                {
                    var savedChild = saved.DynamicChildren[i];
                    if (savedChild != null
                        && i < newNode.DynamicChildren.Count
                        && nodeMapping.TryGetValue(savedChild, out var mappedChild))
                    {
                        newNode.DynamicChildren[i] = mappedChild;
                    }
                }
            }

            Dispatcher.BeginInvoke(
                System.Windows.Threading.DispatcherPriority.Loaded,
                new Action(() => renderer.RedrawConnections()));
            Log($"Graph restored with {allNodes.Count} nodes.");
        }

        private void RestoreGraphFromFile(GraphFileModel graphFile)
        {
            var nodeMapping = new Dictionary<int, NodeVisual>();

            foreach (var saved in graphFile.Nodes)
            {
                var newNode = CreateNodeFromFileModel(saved);
                nodeMapping[saved.Id] = newNode;
                AddNodeToCanvas(newNode);
            }

            foreach (var saved in graphFile.Nodes)
            {
                if (!nodeMapping.TryGetValue(saved.Id, out var newNode))
                {
                    continue;
                }

                if (saved.NextNodeId.HasValue && nodeMapping.TryGetValue(saved.NextNodeId.Value, out var next))
                {
                    newNode.Next = next;
                }
                if (saved.TrueChildId.HasValue && nodeMapping.TryGetValue(saved.TrueChildId.Value, out var trueNode))
                {
                    newNode.TrueChild = trueNode;
                }
                if (saved.FalseChildId.HasValue && nodeMapping.TryGetValue(saved.FalseChildId.Value, out var falseNode))
                {
                    newNode.FalseChild = falseNode;
                }

                for (var i = 0; i < saved.DynamicChildIds.Count && i < newNode.DynamicChildren.Count; i++)
                {
                    var childId = saved.DynamicChildIds[i];
                    if (childId.HasValue && nodeMapping.TryGetValue(childId.Value, out var childNode))
                    {
                        newNode.DynamicChildren[i] = childNode;
                    }
                }
            }

            Dispatcher.BeginInvoke(
                System.Windows.Threading.DispatcherPriority.Loaded,
                new Action(() => renderer.RedrawConnections()));
            Log($"Graph restored with {allNodes.Count} nodes.");
        }

        private NodeVisual CreateNodeFromFileModel(NodeVisualFileModel saved)
        {
            var nodeType = ResolveNodeType(saved.NodeTypeName, saved.IsRoot);
            var color = saved.IsRoot ? Brushes.DarkGreen : GetNodeColor(saved.Category);
            var title = saved.IsRoot ? "OnConnected" : ResolveNodeTitle(nodeType, saved.NodeTypeName);
            var border = CreateNodeVisual(title, color);

            var newNode = new NodeVisual
            {
                Border = border,
                Category = saved.Category,
                InputPort = CreateInputPort(),
                IsRoot = saved.IsRoot,
                NodeType = nodeType,
                Configuration = CloneConfiguration(saved.Configuration),
                NextPortType = saved.NextPortType,
                TruePortType = saved.TruePortType,
                FalsePortType = saved.FalsePortType
            };

            if (saved.IsRoot)
            {
                newNode.ActionNode = new CustomActionNode
                {
                    Name = "OnConnected",
                    Trigger = new TriggerCondition { Type = TriggerType.OnConnected }
                };
            }

            if (newNode.NodeType == typeof(RandomChoiceNode))
            {
                CreateDynamicPorts(newNode, saved.DynamicPortTypes.Count);
            }
            else if (!saved.IsRoot && saved.Category == NodeCategory.Action)
            {
                newNode.OutputPort = CreateOutputPort("default");
            }
            else if (!saved.IsRoot)
            {
                newNode.OutputPortTrue = CreateOutputPort(
                    saved.Category == NodeCategory.Condition ? "true" : "continue");
                newNode.OutputPortFalse = CreateOutputPort(
                    saved.Category == NodeCategory.Condition ? "false" : "exit");
            }
            else
            {
                newNode.OutputPort = CreateOutputPort("default");
            }

            WpfCanvas.SetLeft(border, saved.Left);
            WpfCanvas.SetTop(border, saved.Top);
            return newNode;
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

            if (original.NodeType != typeof(RandomChoiceNode))
            {
                if (original.Category == NodeCategory.Action)
                {
                    newNode.OutputPort = CreateOutputPort("default");
                }
                else
                {
                    newNode.OutputPortTrue = CreateOutputPort(
                        original.Category == NodeCategory.Condition ? "true" : "continue");
                    newNode.OutputPortFalse = CreateOutputPort(
                        original.Category == NodeCategory.Condition ? "false" : "exit");
                }
            }

            WpfCanvas.SetLeft(border, position.left);
            WpfCanvas.SetTop(border, position.top);
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

        private static NodeConfiguration? CloneConfiguration(NodeConfigurationFileModel? original)
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
                Properties = original.Properties.ToDictionary(
                    pair => pair.Key,
                    pair => ConvertJsonElementToPropertyValue(pair.Value))
            };
        }

        private GraphFileModel CreateGraphFileModel()
        {
            var nodeIds = allNodes
                .Select((node, index) => new { node, id = index + 1 })
                .ToDictionary(x => x.node, x => x.id);

            return new GraphFileModel
            {
                Name = BuiltGraph?.Name ?? "Bot Action Graph",
                Nodes = allNodes.Select(node => new NodeVisualFileModel
                {
                    Id = nodeIds[node],
                    IsRoot = node.IsRoot,
                    NodeTypeName = node.NodeType?.AssemblyQualifiedName,
                    Category = node.Category,
                    Left = NormalizeCanvasPosition(WpfCanvas.GetLeft(node.Border)),
                    Top = NormalizeCanvasPosition(WpfCanvas.GetTop(node.Border)),
                    Configuration = CreateConfigurationFileModel(node.Configuration),
                    NextPortType = node.NextPortType,
                    TruePortType = node.TruePortType,
                    FalsePortType = node.FalsePortType,
                    NextNodeId = node.Next != null && nodeIds.TryGetValue(node.Next, out var nextId) ? nextId : null,
                    TrueChildId = node.TrueChild != null && nodeIds.TryGetValue(node.TrueChild, out var trueId) ? trueId : null,
                    FalseChildId = node.FalseChild != null && nodeIds.TryGetValue(node.FalseChild, out var falseId) ? falseId : null,
                    DynamicPortTypes = [.. node.DynamicPortTypes],
                    DynamicChildIds = node.DynamicChildren
                        .Select(child => child != null && nodeIds.TryGetValue(child, out var childId) ? childId : (int?)null)
                        .ToList()
                }).ToList()
            };
        }

        private static NodeConfigurationFileModel? CreateConfigurationFileModel(NodeConfiguration? configuration)
        {
            if (configuration == null)
            {
                return null;
            }

            return new NodeConfigurationFileModel
            {
                PacketId = configuration.PacketId,
                StringValue = configuration.StringValue,
                IntValue = configuration.IntValue,
                Properties = configuration.Properties.ToDictionary(
                    pair => pair.Key,
                    pair => JsonSerializer.SerializeToElement(pair.Value, pair.Value?.GetType() ?? typeof(object)))
            };
        }

        private void ClearCurrentGraph()
        {
            selectedNode = null;
            BuiltGraph = null;

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

        private static double NormalizeCanvasPosition(double value) => double.IsNaN(value) ? 0 : value;

        private static Type? ResolveNodeType(string? nodeTypeName, bool isRoot)
        {
            if (isRoot)
            {
                return null;
            }

            if (string.IsNullOrWhiteSpace(nodeTypeName))
            {
                throw new InvalidOperationException("Loaded graph node is missing its type name.");
            }

            var resolvedType = Type.GetType(nodeTypeName, throwOnError: false);
            if (resolvedType != null)
            {
                return resolvedType;
            }

            var simpleTypeName = nodeTypeName.Split(',')[0].Trim();
            resolvedType = typeof(ActionNodeBase).Assembly.GetType(simpleTypeName, throwOnError: false);
            if (resolvedType != null)
            {
                return resolvedType;
            }

            throw new InvalidOperationException($"Failed to resolve node type '{nodeTypeName}'.");
        }

        private static string ResolveNodeTitle(Type? nodeType, string? nodeTypeName)
        {
            if (nodeType != null)
            {
                return nodeType.Name;
            }

            if (string.IsNullOrWhiteSpace(nodeTypeName))
            {
                return "Unknown";
            }

            var typeName = nodeTypeName.Split(',')[0].Trim();
            var lastDotIndex = typeName.LastIndexOf('.');
            return lastDotIndex >= 0 ? typeName[(lastDotIndex + 1)..] : typeName;
        }

        private static object ConvertJsonElementToPropertyValue(JsonElement element)
        {
            return element.ValueKind switch
            {
                JsonValueKind.String => element.GetString() ?? string.Empty,
                JsonValueKind.Number => element.TryGetInt32(out var intValue)
                    ? intValue
                    : element.TryGetInt64(out var longValue)
                        ? longValue
                        : element.GetDouble(),
                JsonValueKind.True => true,
                JsonValueKind.False => false,
                JsonValueKind.Null => string.Empty,
                _ => element.GetRawText()
            };
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

        private void ShowStatsWindow_Click(object sender, RoutedEventArgs e)
        {
            if (statsWindow?.IsVisible == true) 
            { 
                statsWindow.Activate(); return; 
            }
            
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
            if (FindName("StatusText") is not TextBlock tb)
            {
                return;
            }

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

            interaction?.Cleanup();

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

            BotTesterCore.Instance.ClearSavedGraphVisuals();
        }

        private void Log(string msg)
        {
            if (LogListBox.Items.Count >= MaxLogItems)
            {
                LogListBox.Items.RemoveAt(0);
            }

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
