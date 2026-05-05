using Microsoft.Extensions.Configuration;
using Microsoft.Win32;
using MultiSocketRUDPBotTester.Bot;
using MultiSocketRUDPBotTester.CanvasRenderer;
using MultiSocketRUDPBotTester.ClientCore;
using MultiSocketRUDPBotTester.Evaluation;
using MultiSocketRUDPBotTester.Graph;
using MultiSocketRUDPBotTester.Windows;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Shapes;
using static MultiSocketRUDPBotTester.Bot.NodeExecutionStats;
using Path = System.IO.Path;
using WpfCanvas = System.Windows.Controls.Canvas;

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

        /// <summary>
        /// 봇 액션 그래프 파일을 직렬화/역직렬화할 때 사용되는 JSON 직렬화 옵션입니다.
        /// </summary>
        /// <remarks>
        /// 이 옵션은 JSON 출력을 가독성 있게 들여쓰기하고, 열거형 값을 문자열로 변환하여 처리하도록 설정합니다.
        /// </remarks>
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

        /// <summary>
        /// "Save Graph" 버튼 클릭 이벤트를 처리합니다.
        /// 현재의 봇 액션 그래프를 JSON 형식으로 파일에 저장합니다.
        /// </summary>
        /// <param name="sender">이벤트를 발생시킨 객체입니다.</param>
        /// <param name="e">이벤트 데이터를 포함하는 <see cref="RoutedEventArgs"/>입니다.</param>
        /// <remarks>
        /// 사용자가 파일을 저장할 위치를 선택하면, <see cref="CreateGraphFileModel"/>을 통해 현재 그래프 데이터를 가져와
        /// <see cref="JsonSerializer.Serialize"/>를 사용하여 JSON 문자열로 변환한 후 해당 파일에 기록합니다.
        /// 성공 또는 실패 시 로그 및 UI 상태 텍스트를 업데이트합니다.
        /// </remarks>
        /// <failure
        ///   - 사용자가 파일 저장 대화 상자를 취소하면 아무런 동작 없이 함수를 종료합니다.
        ///   - 파일 저장 중 예외가 발생하면 오류 메시지를 로그하고 사용자에게 경고 메시지 박스를 표시하며, UI 상태를 업데이트합니다.
        /// </failure>
        /// <sideeffect
        ///   - 파일 시스템에 봇 그래프 JSON 파일이 생성되거나 갱신됩니다.
        ///   - <see cref="Log"/>를 통해 작업 진행 상황이 기록됩니다.
        ///   - UI 상태 텍스트(<see cref="SetStatusText"/>)와 색상이 업데이트됩니다.
        ///   - 오류 발생 시 메시지 박스가 표시됩니다.
        /// </sideeffect>
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

        /// <summary>
        /// "Load Graph" 버튼 클릭 이벤트를 처리합니다.
        /// 사용자가 선택한 JSON 파일로부터 봇 액션 그래프를 로드하여 UI에 표시합니다.
        /// </summary>
        /// <param name="sender">이벤트를 발생시킨 객체입니다.</param>
        /// <param name="e">이벤트 데이터를 포함하는 <see cref="RoutedEventArgs"/>입니다.</param>
        /// <remarks>
        /// 파일 선택 대화 상자를 통해 파일을 선택하면, 파일 내용을 읽어 <see cref="GraphFileModel"/> 객체로 역직렬화합니다.
        /// 기존 그래프를 지우고 <see cref="RestoreGraphFromFile"/>을 호출하여 로드된 데이터로 새 그래프를 구성합니다.
        /// </remarks>
        /// <failure
        ///   - 사용자가 파일 열기 대화 상자를 취소하면 아무런 동작 없이 함수를 종료합니다.
        ///   - 선택된 파일이 비어있거나 유효하지 않은 그래프 구조를 가질 경우 <see cref="InvalidOperationException"/>을 발생시킵니다.
        ///   - 파일 읽기 또는 JSON 역직렬화 중 예외가 발생하면 오류 메시지를 로그하고 사용자에게 경고 메시지 박스를 표시하며, UI 상태를 업데이트합니다.
        /// </failure>
        /// <sideeffect
        ///   - 파일 시스템으로부터 JSON 그래프 파일 내용을 읽어옵니다.
        ///   - 현재 UI에 표시된 그래프와 내부 데이터를 <see cref="ClearCurrentGraph"/>를 통해 초기화합니다.
        ///   - <see cref="RestoreGraphFromFile"/> 호출을 통해 UI에 새로운 그래프 노드와 연결이 그려집니다.
        ///   - <see cref="BuiltGraph"/> 필드가 null로 설정됩니다.
        ///   - <see cref="Log"/>를 통해 작업 진행 상황이 기록됩니다.
        ///   - UI 상태 텍스트(<see cref="SetStatusText"/>)와 색상이 업데이트됩니다.
        ///   - 오류 발생 시 메시지 박스가 표시됩니다.
        /// </sideeffect>
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

        /// <summary>
        /// 주어진 <see cref="GraphFileModel"/>을 기반으로 봇 액션 그래프를 UI에 재구성합니다.
        /// 이 함수는 저장된 노드 데이터를 읽어 <see cref="NodeVisual"/> 객체를 생성하고, 이들 간의 연결을 설정한 후 UI를 업데이트합니다.
        /// </summary>
        /// <param name="graphFile">로드할 그래프 데이터를 포함하는 <see cref="GraphFileModel"/> 객체입니다.</param>
        /// <remarks>
        ///   - 첫 번째 단계에서는 파일 모델의 각 노드에 대해 시각적 노드(<see cref="NodeVisual"/>)를 생성하고 캔버스에 추가합니다.
        ///   - 두 번째 단계에서는 생성된 시각적 노드들 간의 관계(Next, TrueChild, FalseChild, DynamicChildren)를 파일 모델에 정의된 ID를 사용하여 연결합니다.
        ///   - 마지막으로 디스패처를 통해 UI 스레드에서 모든 연결을 다시 그리도록 요청합니다.
        /// </remarks>
        /// <sideeffect
        ///   - <see cref="CreateNodeFromFileModel"/>을 호출하여 새로운 <see cref="NodeVisual"/> 인스턴스가 생성됩니다.
        ///   - <see cref="AddNodeToCanvas"/>를 호출하여 생성된 노드들이 UI 캔버스에 추가되고, 내부 `allNodes` 컬렉션이 업데이트됩니다.
        ///   - <see cref="NodeVisual"/> 객체 간의 참조(Next, TrueChild, FalseChild, DynamicChildren)가 설정되어 그래프 구조가 재구성됩니다.
        ///   - UI 스레드에서 <see cref="renderer.RedrawConnections"/>가 호출되어 캔버스의 연결선이 다시 그려집니다.
        ///   - <see cref="Log"/>를 통해 그래프 복원 완료 메시지가 기록됩니다.
        /// </sideeffect>
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

        /// <summary>
        /// 파일에서 로드된 <see cref="NodeVisualFileModel"/> 데이터를 사용하여 새로운 <see cref="NodeVisual"/> 객체를 생성합니다.
        /// 이 함수는 노드의 유형, 색상, 제목 및 포트 구성을 설정하고, 특정 노드 유형에 대한 추가 초기화를 수행합니다.
        /// </summary>
        /// <param name="saved">파일에서 로드된 노드 데이터를 포함하는 <see cref="NodeVisualFileModel"/> 객체입니다.</param>
        /// <returns>
        /// 초기화된 <see cref="NodeVisual"/> 객체입니다.
        /// </returns>
        /// <remarks>
        ///   - 노드의 유형과 루트 여부에 따라 시각적 요소(색상, 제목)와 내부 액션 노드(<see cref="CustomActionNode"/>)를 설정합니다.
        ///   - 노드 카테고리 및 유형에 따라 적절한 입력 및 출력 포트(<see cref="OutputPort"/>, <see cref="OutputPortTrue"/>, <see cref="OutputPortFalse"/>, 동적 포트)를 생성합니다.
        ///   - 노드의 위치(Left, Top)를 설정합니다.
        /// </remarks>
        /// <sideeffect
        ///   - <see cref="CreateNodeVisual"/>, <see cref="CreateInputPort"/>, <see cref="CreateOutputPort"/>, <see cref="CreateDynamicPorts"/> 등의 헬퍼 함수를 호출하여 새로운 UI 요소 및 포트 객체를 생성합니다.
        ///   - <see cref="CloneConfiguration"/>을 호출하여 <see cref="NodeConfiguration"/> 객체를 복제합니다.
        ///   - 루트 노드인 경우 <see cref="CustomActionNode"/> 인스턴스가 생성되어 <see cref="NodeVisual.ActionNode"/>에 할당됩니다.
        ///   - <see cref="WpfCanvas.SetLeft"/> 및 <see cref="WpfCanvas.SetTop"/>을 호출하여 UI 요소의 위치를 설정합니다.
        /// </sideeffect>
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

        /// <summary>
        /// <see cref="NodeConfigurationFileModel"/> 객체의 데이터를 기반으로 새로운 <see cref="NodeConfiguration"/> 객체를 생성하여 반환합니다.
        /// 이는 파일 모델의 설정을 런타임에 사용될 수 있는 실제 구성 객체로 변환하는 역할을 합니다.
        /// </summary>
        /// <param name="original">복제할 원본 <see cref="NodeConfigurationFileModel"/> 객체입니다. null일 수 있습니다.</param>
        /// <returns>
        /// 원본이 null이면 null을 반환하고, 그렇지 않으면 원본의 데이터를 복사한 새로운 <see cref="NodeConfiguration"/> 객체를 반환합니다.
        /// </returns>
        /// <remarks>
        /// Properties 컬렉션은 <see cref="ConvertJsonElementToPropertyValue"/>를 사용하여 <see cref="JsonElement"/>를 적절한 속성 값 타입으로 변환합니다.
        /// </remarks>
        /// <sideeffect
        ///   - 새로운 <see cref="NodeConfiguration"/> 인스턴스와 해당 Properties 딕셔너리가 생성됩니다.
        ///   - <see cref="ConvertJsonElementToPropertyValue"/> 헬퍼 함수가 호출됩니다.
        /// </sideeffect>
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

        /// <summary>
        /// 현재 시각적 그래프 데이터를 기반으로 저장 가능한 `GraphFileModel` 객체를 생성합니다.
        /// 이 모델은 그래프의 직렬화 및 저장을 위해 사용됩니다.
        /// </summary>
        /// <returns>생성된 `GraphFileModel` 인스턴스.</returns>
        /// <remarks>
        /// <para>상태 변화:</para>
        /// <list type="bullet">
        /// <item>내부 `allNodes` 컬렉션의 `NodeVisual` 객체들을 `GraphFileModel`의 `NodeVisualFileModel` 리스트로 변환합니다.</item>
        /// <item>각 `NodeVisual`에 대해 고유 ID를 부여하고, 노드 간의 연결(`NextNodeId`, `TrueChildId`, `FalseChildId`, `DynamicChildIds`)을 이 ID를 기반으로 참조합니다.</item>
        /// </list>
        /// <para>실패 조건:</para>
        /// <list type="bullet">
        /// <item>연결된 노드(`Next`, `TrueChild`, `FalseChild`, `DynamicChildren`)가 `allNodes` 컬렉션에 없으면 해당 ID는 `null`이 됩니다.</item>
        /// <item>`BuiltGraph`가 `null`인 경우, `Name` 속성은 "Bot Action Graph"로 기본 설정됩니다.</item>
        /// </list>
        /// <para>Side Effects:</para>
        /// <list type="bullet">
        /// <item>클래스 멤버의 직접적인 상태 변경은 없으며, 내부 데이터를 기반으로 새로운 모델 객체를 생성하여 반환합니다.</item>
        /// </list>
        /// </remarks>
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

        /// <summary>
        /// `NodeConfiguration` 객체를 직렬화에 적합한 `NodeConfigurationFileModel`로 변환합니다.
        /// </summary>
        /// <param name="configuration">변환할 `NodeConfiguration` 객체.</param>
        /// <returns>변환된 `NodeConfigurationFileModel` 객체 또는 입력이 `null`인 경우 `null`.</returns>
        /// <remarks>
        /// <para>상태 변화:</para>
        /// <list type="bullet">
        /// <item>입력 `configuration` 객체의 속성들을 `NodeConfigurationFileModel`의 해당 속성들로 매핑합니다.</item>
        /// <item>`Properties` 딕셔너리의 값은 `JsonSerializer.SerializeToElement`를 사용하여 `JsonElement`로 직렬화됩니다.</item>
        /// </list>
        /// <para>실패 조건:</para>
        /// <list type="bullet">
        /// <item>입력 `configuration`이 `null`인 경우, 함수는 `null`을 반환합니다.</item>
        /// </list>
        /// <para>Side Effects:</para>
        /// <list type="bullet">
        /// <item>클래스 인스턴스의 상태를 변경하지 않고, 새로운 모델 객체를 생성하여 반환합니다.</item>
        /// </list>
        /// </remarks>
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

        /// <summary>
        /// 현재 로드되거나 생성된 그래프의 모든 시각적 요소와 내부 상태를 초기화합니다.
        /// </summary>
        /// <remarks>
        /// <para>상태 변화:</para>
        /// <list type="bullet">
        /// <item>`selectedNode`를 `null`로 설정하여 현재 선택된 노드를 해제합니다.</item>
        /// <item>`BuiltGraph`를 `null`로 설정하여 빌드된 그래프 모델을 제거합니다.</item>
        /// <item>`allNodes` 컬렉션을 비워 모든 노드 데이터와 참조를 제거합니다.</item>
        /// <item>`GraphCanvas.Children` 컬렉션을 비워 UI에서 모든 시각적 노드를 제거합니다.</item>
        /// <item>각 노드의 컨텍스트 메뉴를 클리어하고 `null`로 설정합니다.</item>
        /// </list>
        /// <para>실패 조건:</para>
        /// <list type="bullet">
        /// <item>특정 노드의 `Border` 또는 `ContextMenu`가 `null`인 경우, 해당 노드의 컨텍스트 메뉴 처리는 건너뜁니다.</item>
        /// </list>
        /// <para>Side Effects:</para>
        /// <list type="bullet">
        /// <item>UI (`GraphCanvas.Children`)를 직접적으로 변경하여 화면에서 그래프를 제거합니다.</item>
        /// <item>클래스의 내부 상태(`selectedNode`, `BuiltGraph`, `allNodes`)를 초기 상태로 재설정합니다.</item>
        /// </list>
        /// </remarks>
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

        /// <summary>
        /// WPF 캔버스 위치 값으로 사용할 double 값을 정규화합니다.
        /// 입력 값이 `double.NaN`인 경우 `0`을 반환하고, 그렇지 않으면 원래 값을 반환합니다.
        /// </summary>
        /// <param name="value">정규화할 double 값.</param>
        /// <returns>정규화된 double 값.</returns>
        /// <remarks>
        /// <para>상태 변화:</para>
        /// <list type="bullet">
        /// <item>없음.</item>
        /// </list>
        /// <para>실패 조건:</para>
        /// <list type="bullet">
        /// <item>없음. 항상 유효한 double 값을 반환합니다.</item>
        /// </list>
        /// <para>Side Effects:</para>
        /// <list type="bullet">
        /// <item>없음.</item>
        /// </list>
        /// </remarks>
        private static double NormalizeCanvasPosition(double value) => double.IsNaN(value) ? 0 : value;

        /// <summary>
        /// 노드 유형의 어셈블리 정규화된 이름으로부터 `Type` 객체를 해결합니다.
        /// 루트 노드에 대한 특별한 처리와 실패 시 대체 해결 메커니즘을 포함합니다.
        /// </summary>
        /// <param name="nodeTypeName">해결할 노드 유형의 이름 (어셈블리 정규화된 이름일 수 있음).</param>
        /// <param name="isRoot">해당 노드가 루트 노드인지 여부.</param>
        /// <returns>해결된 `Type` 객체 또는 `isRoot`가 `true`일 경우 `null`.</returns>
        /// <exception cref="InvalidOperationException">노드 유형 이름이 없거나, 어셈블리 내에서 유형을 해결할 수 없는 경우 발생합니다.</exception>
        /// <remarks>
        /// <para>상태 변화:</para>
        /// <list type="bullet">
        /// <item>없음.</item>
        /// </list>
        /// <para>실패 조건:</para>
        /// <list type="bullet">
        /// <item>`isRoot`가 `true`인 경우, `nodeTypeName`과 관계없이 즉시 `null`을 반환합니다.</item>
        /// <item>`nodeTypeName`이 `null`이거나 공백이고 `isRoot`가 `false`인 경우, `InvalidOperationException`을 발생시킵니다.</item>
        /// <item>지정된 `nodeTypeName`으로 `Type.GetType` 호출에 실패하면, 어셈블리 이름 부분을 제거하고 `ActionNodeBase` 어셈블리 내에서 다시 시도합니다.</item>
        /// <item>모든 시도에도 불구하고 유형을 해결할 수 없는 경우, `InvalidOperationException`을 발생시킵니다.</item>
        /// </list>
        /// <para>Side Effects:</para>
        /// <list type="bullet">
        /// <item>없음.</n></list>
        /// </remarks>
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

        /// <summary>
        /// 노드의 표시 제목을 결정합니다. 제공된 `Type` 객체가 있으면 그 이름을 사용하고, 그렇지 않으면 유형 이름 문자열에서 파싱합니다.
        /// </summary>
        /// <param name="nodeType">노드의 `Type` 객체 (있을 경우).</param>
        /// <param name="nodeTypeName">노드의 어셈블리 정규화된 유형 이름 (있을 경우).</param>
        /// <returns>노드의 표시 제목.</returns>
        /// <remarks>
        /// <para>상태 변화:</para>
        /// <list type="bullet">
        /// <item>없음.</item>
        /// </list>
        /// <para>실패 조건:</para>
        /// <list type="bullet">
        /// <item>`nodeType`과 `nodeTypeName`이 모두 `null`이거나 공백인 경우, "Unknown"을 반환합니다.</item>
        /// </list>
        /// <para>Side Effects:</para>
        /// <list type="bullet">
        /// <item>없음.</item>
        /// </list>
        /// </remarks>
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

        /// <summary>
        /// `JsonElement`의 `JsonValueKind`에 따라 해당 `JsonElement`를 .NET 기본 객체 유형으로 변환합니다.
        /// </summary>
        /// <param name="element">변환할 `JsonElement`.</param>
        /// <returns>변환된 .NET 객체.</returns>
        /// <remarks>
        /// <para>상태 변화:</para>
        /// <list type="bullet">
        /// <item>없음.</item>
        /// </list>
        /// <para>실패 조건:</para>
        /// <list type="bullet">
        /// <item>`JsonValueKind.String`이 `null`인 경우, `string.Empty`를 반환합니다.</item>
        /// <item>`JsonValueKind.Number`인 경우 `int`, `long`, `double` 순서로 변환을 시도합니다.</item>
        /// <item>`JsonValueKind.Null`은 `string.Empty`로 변환됩니다.</item>
        /// <item>정의된 `JsonValueKind` 이외의 값(예: `Object`, `Array`)은 `element.GetRawText()`를 반환합니다.</item>
        /// </list>
        /// <para>Side Effects:</para>
        /// <list type="bullet">
        /// <item>없음.</item>
        /// </list>
        /// </remarks>
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
