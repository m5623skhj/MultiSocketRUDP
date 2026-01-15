using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Shapes;
using MultiSocketRUDPBotTester.Bot;
using MultiSocketRUDPBotTester.ClientCore;
using MultiSocketRUDPBotTester.Buffer;
using System.Xml.Linq;
using static MultiSocketRUDPBotTester.Bot.WaitForPacketNode;

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

        private void ShowNodeConfigurationDialog(NodeVisual node)
        {
            var dialog = new Window
            {
                Title = $"Configure {node.NodeType?.Name}",
                Width = 400,
                Height = 300,
                WindowStartupLocation = WindowStartupLocation.CenterOwner,
                Owner = this
            };

            var stack = new StackPanel { Margin = new Thickness(10) };

            if (node.NodeType == typeof(SendPacketNode))
            {
                stack.Children.Add(new TextBlock { Text = "Packet ID:", FontWeight = FontWeights.Bold });
                var packetIdCombo = new ComboBox
                {
                    ItemsSource = Enum.GetValues(typeof(PacketId)),
                    SelectedItem = node.Configuration?.PacketId ?? PacketId.INVALID_PACKET_ID
                };
                stack.Children.Add(packetIdCombo);

                stack.Children.Add(new TextBlock { Text = "Packet Builder (Custom Code):", FontWeight = FontWeights.Bold, Margin = new Thickness(0, 10, 0, 0) });
                var codeBox = new TextBox
                {
                    Height = 100,
                    AcceptsReturn = true,
                    TextWrapping = TextWrapping.Wrap,
                    Text = node.Configuration?.StringValue ?? "Write packet builder code"
                };
                stack.Children.Add(codeBox);

                var saveBtn = new Button { Content = "Save", Margin = new Thickness(0, 10, 0, 0) };
                saveBtn.Click += (_, _) =>
                {
                    node.Configuration ??= new NodeConfiguration();
                    node.Configuration.PacketId = (PacketId)packetIdCombo.SelectedItem;
                    node.Configuration.StringValue = codeBox.Text;
                    Log($"Configuration saved for {node.NodeType.Name}");
                    dialog.Close();
                };
                stack.Children.Add(saveBtn);
            }
            else if (node.NodeType == typeof(DelayNode))
            {
                stack.Children.Add(new TextBlock { Text = "Delay (ms):", FontWeight = FontWeights.Bold });
                var delayBox = new TextBox
                {
                    Text = node.Configuration?.IntValue.ToString() ?? "1000"
                };
                stack.Children.Add(delayBox);

                var saveBtn = new Button { Content = "Save", Margin = new Thickness(0, 10, 0, 0) };
                saveBtn.Click += (_, _) =>
                {
                    if (int.TryParse(delayBox.Text, out var delay))
                    {
                        node.Configuration ??= new NodeConfiguration();
                        node.Configuration.IntValue = delay;
                        Log($"Delay set to {delay}ms");
                        dialog.Close();
                    }
                    else
                    {
                        MessageBox.Show("Invalid delay value", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                    }
                };
                stack.Children.Add(saveBtn);
            }
            else if (node.NodeType == typeof(LogNode))
			{
				stack.Children.Add(new TextBlock 
				{ 
					Text = "Log Message:", 
					FontWeight = FontWeights.Bold,
					Margin = new Thickness(0, 0, 0, 5)
				});
				
				stack.Children.Add(new TextBlock 
				{ 
					Text = "Enter the message to log when this node executes:",
					FontSize = 11,
					Foreground = Brushes.Gray,
					Margin = new Thickness(0, 0, 0, 5),
					TextWrapping = TextWrapping.Wrap
				});
			
				var logMessageBox = new TextBox
				{
					Height = 120,
					AcceptsReturn = true,
					TextWrapping = TextWrapping.Wrap,
					VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
					Text = node.Configuration?.StringValue ?? "Enter your log message here...",
					Margin = new Thickness(0, 0, 0, 10)
				};
				stack.Children.Add(logMessageBox);
			
				var saveBtn = new Button 
				{ 
					Content = "Save", 
					Height = 30,
					FontWeight = FontWeights.Bold
				};
                
				saveBtn.Click += (_, _) =>
				{
					var message = logMessageBox.Text.Trim();
					if (string.IsNullOrEmpty(message))
					{
						MessageBox.Show("Log message cannot be empty", "Warning", 
							MessageBoxButton.OK, MessageBoxImage.Warning);
						return;
					}
			
					node.Configuration ??= new NodeConfiguration();
					node.Configuration.StringValue = message;
					Log($"Log message configured: {message}");
                    
					dialog.Close();
				};
                
				stack.Children.Add(saveBtn);
			}
            else if (node.NodeType == typeof(ConditionalNode))
            {
                stack.Children.Add(new TextBlock
                {
                    Text = "Conditional Expression:",
                    FontWeight = FontWeights.Bold,
                    Margin = new Thickness(0, 0, 0, 5)
                });

                stack.Children.Add(new TextBlock
                {
                    Text = "Select a getter function or enter a constant value",
                    FontSize = 11,
                    Foreground = Brushes.Gray,
                    Margin = new Thickness(0, 0, 0, 10),
                    TextWrapping = TextWrapping.Wrap
                });

                // Left Value
                var leftPanel = new StackPanel { Margin = new Thickness(0, 0, 0, 10) };
                leftPanel.Children.Add(new TextBlock { Text = "Left Value:", FontWeight = FontWeights.Bold, Margin = new Thickness(0, 0, 0, 5) });

                var leftTypePanel = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 5) };
                leftTypePanel.Children.Add(new TextBlock { Text = "Type:", Width = 80 });
                var leftTypeCombo = new ComboBox
                {
                    Width = 220,
                    ItemsSource = new[] { "Getter Function", "Constant" },
                    SelectedItem = node.Configuration?.Properties.GetValueOrDefault("LeftType")?.ToString() ?? "Constant"
                };
                leftTypePanel.Children.Add(leftTypeCombo);
                leftPanel.Children.Add(leftTypePanel);

                var leftGetterCombo = new ComboBox
                {
                    Width = 300,
                    DisplayMemberPath = "DisplayName",
                    SelectedValuePath = "Method.Name",
                    ItemsSource = VariableAccessorRegistry.GetAllGetters(),
                    Visibility = Visibility.Collapsed
                };

                var leftConstantBox = new TextBox
                {
                    Width = 300,
                    Text = node.Configuration?.Properties.GetValueOrDefault("Left")?.ToString() ?? "0"
                };

                leftTypeCombo.SelectionChanged += (_, _) =>
                {
                    if (leftTypeCombo.SelectedItem?.ToString() == "Getter Function")
                    {
                        leftGetterCombo.Visibility = Visibility.Visible;
                        leftConstantBox.Visibility = Visibility.Collapsed;
                    }
                    else
                    {
                        leftGetterCombo.Visibility = Visibility.Collapsed;
                        leftConstantBox.Visibility = Visibility.Visible;
                    }
                };

                leftPanel.Children.Add(leftGetterCombo);
                leftPanel.Children.Add(leftConstantBox);
                stack.Children.Add(leftPanel);

                // Operator
                var opPanel = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 5) };
                opPanel.Children.Add(new TextBlock { Text = "Operator:", Width = 80, VerticalAlignment = VerticalAlignment.Center });
                var opCombo = new ComboBox
                {
                    Width = 220,
                    ItemsSource = new[] { ">", "<", "==", ">=", "<=", "!=" },
                    SelectedItem = node.Configuration?.Properties.GetValueOrDefault("Op")?.ToString() ?? ">"
                };
                opPanel.Children.Add(opCombo);
                stack.Children.Add(opPanel);

                // Right Value
                var rightPanel = new StackPanel { Margin = new Thickness(0, 0, 0, 10) };
                rightPanel.Children.Add(new TextBlock { Text = "Right Value:", FontWeight = FontWeights.Bold, Margin = new Thickness(0, 0, 0, 5) });

                var rightTypePanel = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 5) };
                rightTypePanel.Children.Add(new TextBlock { Text = "Type:", Width = 80 });
                var rightTypeCombo = new ComboBox
                {
                    Width = 220,
                    ItemsSource = new[] { "Getter Function", "Constant" },
                    SelectedItem = node.Configuration?.Properties.GetValueOrDefault("RightType")?.ToString() ?? "Constant"
                };
                rightTypePanel.Children.Add(rightTypeCombo);
                rightPanel.Children.Add(rightTypePanel);

                var rightGetterCombo = new ComboBox
                {
                    Width = 300,
                    DisplayMemberPath = "DisplayName",
                    SelectedValuePath = "Method.Name",
                    ItemsSource = VariableAccessorRegistry.GetAllGetters(),
                    Visibility = Visibility.Collapsed
                };

                var rightConstantBox = new TextBox
                {
                    Width = 300,
                    Text = node.Configuration?.Properties.GetValueOrDefault("Right")?.ToString() ?? "0"
                };

                rightTypeCombo.SelectionChanged += (_, _) =>
                {
                    if (rightTypeCombo.SelectedItem?.ToString() == "Getter Function")
                    {
                        rightGetterCombo.Visibility = Visibility.Visible;
                        rightConstantBox.Visibility = Visibility.Collapsed;
                    }
                    else
                    {
                        rightGetterCombo.Visibility = Visibility.Collapsed;
                        rightConstantBox.Visibility = Visibility.Visible;
                    }
                };

                rightPanel.Children.Add(rightGetterCombo);
                rightPanel.Children.Add(rightConstantBox);
                stack.Children.Add(rightPanel);

                // 초기 상태 설정
                if (node.Configuration?.Properties.GetValueOrDefault("LeftType")?.ToString() == "Getter Function")
                {
                    leftGetterCombo.Visibility = Visibility.Visible;
                    leftConstantBox.Visibility = Visibility.Collapsed;
                    var savedGetter = node.Configuration?.Properties.GetValueOrDefault("Left")?.ToString();
                    if (savedGetter != null)
                    {
                        leftGetterCombo.SelectedValue = savedGetter;
                    }
                }

                if (node.Configuration?.Properties.GetValueOrDefault("RightType")?.ToString() == "Getter Function")
                {
                    rightGetterCombo.Visibility = Visibility.Visible;
                    rightConstantBox.Visibility = Visibility.Collapsed;
                    var savedGetter = node.Configuration?.Properties.GetValueOrDefault("Right")?.ToString();
                    if (savedGetter != null)
                    {
                        rightGetterCombo.SelectedValue = savedGetter;
                    }
                }

                var saveBtn = new Button { Content = "Save", Margin = new Thickness(0, 10, 0, 0) };
                saveBtn.Click += (_, _) =>
                {
                    node.Configuration ??= new NodeConfiguration();

                    node.Configuration.Properties["LeftType"] = leftTypeCombo.SelectedItem?.ToString() ?? "Constant";
                    if (leftTypeCombo.SelectedItem?.ToString() == "Getter Function")
                    {
                        node.Configuration.Properties["Left"] = leftGetterCombo.SelectedValue?.ToString() ?? "";
                    }
                    else
                    {
                        node.Configuration.Properties["Left"] = leftConstantBox.Text;
                    }

                    node.Configuration.Properties["Op"] = opCombo.SelectedItem?.ToString() ?? ">";

                    node.Configuration.Properties["RightType"] = rightTypeCombo.SelectedItem?.ToString() ?? "Constant";
                    if (rightTypeCombo.SelectedItem?.ToString() == "Getter Function")
                    {
                        node.Configuration.Properties["Right"] = rightGetterCombo.SelectedValue?.ToString() ?? "";
                    }
                    else
                    {
                        node.Configuration.Properties["Right"] = rightConstantBox.Text;
                    }

                    dialog.Close();
                };
                stack.Children.Add(saveBtn);
            }
            else if (node.NodeType == typeof(LoopNode))
            {
                stack.Children.Add(new TextBlock { Text = "Loop Count:" });
                var countBox = new TextBox
                {
                    Text = node.Configuration?.Properties.GetValueOrDefault("LoopCount")?.ToString() ?? "5"
                };
                stack.Children.Add(countBox);

                var saveBtn = new Button { Content = "Save" };
                saveBtn.Click += (_, _) =>
                {
                    if (!int.TryParse(countBox.Text, out var n))
                    {
                        return;
                    }

                    node.Configuration ??= new NodeConfiguration();
                    node.Configuration.Properties["LoopCount"] = n;
                    dialog.Close();
                };
                stack.Children.Add(saveBtn);
            }
            else if (node.NodeType == typeof(PacketParserNode))
            {
                stack.Children.Add(new TextBlock
                {
                    Text = "Parse data from packet using setter function",
                    FontWeight = FontWeights.Bold,
                    Margin = new Thickness(0, 0, 0, 10)
                });

                var setterCombo = new ComboBox
                {
                    Width = 350,
                    DisplayMemberPath = "DisplayName",
                    SelectedValuePath = "Method.Name",
                    ItemsSource = VariableAccessorRegistry.GetAllSetters()
                };

                var savedSetter = node.Configuration?.Properties.GetValueOrDefault("SetterMethod")?.ToString();
                if (savedSetter != null)
                {
                    setterCombo.SelectedValue = savedSetter;
                }

                stack.Children.Add(new TextBlock { Text = "Select Setter:", Margin = new Thickness(0, 0, 0, 5) });
                stack.Children.Add(setterCombo);

                stack.Children.Add(new TextBlock
                {
                    Text = "This will read data from the packet and store it using the selected setter function.",
                    FontSize = 11,
                    Foreground = Brushes.Gray,
                    Margin = new Thickness(0, 10, 0, 0),
                    TextWrapping = TextWrapping.Wrap
                });

                var saveBtn = new Button { Content = "Save", Margin = new Thickness(0, 10, 0, 0) };
                saveBtn.Click += (_, _) =>
                {
                    node.Configuration ??= new NodeConfiguration();
                    node.Configuration.Properties["SetterMethod"] = setterCombo.SelectedValue?.ToString() ?? "";
                    Log($"PacketParserNode configured with setter: {setterCombo.Text}");
                    dialog.Close();
                };
                stack.Children.Add(saveBtn);
            }
            else if (node.NodeType == typeof(DisconnectNode))
            {
                stack.Children.Add(new TextBlock
                {
                    Text = "Disconnect Client",
                    FontWeight = FontWeights.Bold,
                    Margin = new Thickness(0, 0, 0, 10)
                });

                stack.Children.Add(new TextBlock { Text = "Disconnect Reason:", Margin = new Thickness(0, 0, 0, 5) });
                var reasonBox = new TextBox
                {
                    Width = 300,
                    Height = 60,
                    AcceptsReturn = true,
                    TextWrapping = TextWrapping.Wrap,
                    Text = node.Configuration?.StringValue ?? "User requested disconnect"
                };
                stack.Children.Add(reasonBox);

                stack.Children.Add(new TextBlock
                {
                    Text = "This node will gracefully disconnect the client.",
                    FontSize = 11,
                    Foreground = Brushes.Gray,
                    Margin = new Thickness(0, 10, 0, 0),
                    TextWrapping = TextWrapping.Wrap
                });

                var saveBtn = new Button { Content = "Save", Margin = new Thickness(0, 10, 0, 0) };
                saveBtn.Click += (_, _) =>
                {
                    node.Configuration ??= new NodeConfiguration();
                    node.Configuration.StringValue = reasonBox.Text;
                    Log($"DisconnectNode configured: {reasonBox.Text}");
                    dialog.Close();
                };
                stack.Children.Add(saveBtn);
            }
            else if (node.NodeType == typeof(RandomDelayNode))
            {
                stack.Children.Add(new TextBlock
                {
                    Text = "Random Delay Configuration",
                    FontWeight = FontWeights.Bold,
                    Margin = new Thickness(0, 0, 0, 10)
                });

                var minPanel = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 5) };
                minPanel.Children.Add(new TextBlock { Text = "Min Delay (ms):", Width = 120, VerticalAlignment = VerticalAlignment.Center });
                var minBox = new TextBox
                {
                    Width = 180,
                    Text = node.Configuration?.Properties.GetValueOrDefault("MinDelay")?.ToString() ?? "500"
                };
                minPanel.Children.Add(minBox);
                stack.Children.Add(minPanel);

                var maxPanel = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 5) };
                maxPanel.Children.Add(new TextBlock { Text = "Max Delay (ms):", Width = 120, VerticalAlignment = VerticalAlignment.Center });
                var maxBox = new TextBox
                {
                    Width = 180,
                    Text = node.Configuration?.Properties.GetValueOrDefault("MaxDelay")?.ToString() ?? "2000"
                };
                maxPanel.Children.Add(maxBox);
                stack.Children.Add(maxPanel);

                stack.Children.Add(new TextBlock
                {
                    Text = "Delays randomly between min and max milliseconds.\nUseful for simulating human-like behavior.",
                    FontSize = 11,
                    Foreground = Brushes.Gray,
                    Margin = new Thickness(0, 10, 0, 0),
                    TextWrapping = TextWrapping.Wrap
                });

                var saveBtn = new Button { Content = "Save", Margin = new Thickness(0, 10, 0, 0) };
                saveBtn.Click += (_, _) =>
                {
                    if (int.TryParse(minBox.Text, out var min) && int.TryParse(maxBox.Text, out var max))
                    {
                        if (min > max)
                        {
                            MessageBox.Show("Min delay cannot be greater than max delay", "Error",
                                MessageBoxButton.OK, MessageBoxImage.Error);
                            return;
                        }

                        node.Configuration ??= new NodeConfiguration();
                        node.Configuration.Properties["MinDelay"] = min;
                        node.Configuration.Properties["MaxDelay"] = max;
                        Log($"RandomDelayNode configured: {min}-{max}ms");
                        dialog.Close();
                    }
                    else
                    {
                        MessageBox.Show("Invalid delay values", "Error",
                            MessageBoxButton.OK, MessageBoxImage.Error);
                    }
                };
                stack.Children.Add(saveBtn);
            }
            else if (node.NodeType == typeof(WaitForPacketNode))
            {
                stack.Children.Add(new TextBlock
                {
                    Text = "Wait For Packet Configuration",
                    FontWeight = FontWeights.Bold,
                    Margin = new Thickness(0, 0, 0, 10)
                });

                stack.Children.Add(new TextBlock { Text = "Expected Packet ID:", Margin = new Thickness(0, 0, 0, 5) });
                var packetIdCombo = new ComboBox
                {
                    Width = 300,
                    ItemsSource = Enum.GetValues(typeof(PacketId)),
                    SelectedItem = node.Configuration?.PacketId ?? PacketId.INVALID_PACKET_ID
                };
                stack.Children.Add(packetIdCombo);

                var timeoutPanel = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 10, 0, 5) };
                timeoutPanel.Children.Add(new TextBlock { Text = "Timeout (ms):", Width = 120, VerticalAlignment = VerticalAlignment.Center });
                var timeoutBox = new TextBox
                {
                    Width = 180,
                    Text = node.Configuration?.IntValue.ToString() ?? "5000"
                };
                timeoutPanel.Children.Add(timeoutBox);
                stack.Children.Add(timeoutPanel);

                stack.Children.Add(new TextBlock
                {
                    Text = "Waits until the specified packet is received or timeout occurs.\n" +
                           "Use 'continue' output for success path.\n" +
                           "Use 'exit' output for timeout path.",
                    FontSize = 11,
                    Foreground = Brushes.Gray,
                    Margin = new Thickness(0, 10, 0, 0),
                    TextWrapping = TextWrapping.Wrap
                });

                var saveBtn = new Button { Content = "Save", Margin = new Thickness(0, 10, 0, 0) };
                saveBtn.Click += (_, _) =>
                {
                    if (int.TryParse(timeoutBox.Text, out var timeout))
                    {
                        node.Configuration ??= new NodeConfiguration();
                        node.Configuration.PacketId = (PacketId)packetIdCombo.SelectedItem;
                        node.Configuration.IntValue = timeout;
                        Log($"WaitForPacketNode configured: {packetIdCombo.SelectedItem}, timeout={timeout}ms");
                        dialog.Close();
                    }
                    else
                    {
                        MessageBox.Show("Invalid timeout value", "Error",
                            MessageBoxButton.OK, MessageBoxImage.Error);
                    }
                };
                stack.Children.Add(saveBtn);
            }
            else if (node.NodeType == typeof(SetVariableNode))
            {
                stack.Children.Add(new TextBlock
                {
                    Text = "Set a constant value to a variable",
                    FontWeight = FontWeights.Bold,
                    Margin = new Thickness(0, 0, 0, 10)
                });

                // Variable Name
                var varPanel = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 5) };
                varPanel.Children.Add(new TextBlock { Text = "Variable name:", Width = 120, VerticalAlignment = VerticalAlignment.Center });
                var varNameBox = new TextBox
                {
                    Width = 180,
                    Text = node.Configuration?.Properties.GetValueOrDefault("VariableName")?.ToString() ?? "myVar"
                };
                varPanel.Children.Add(varNameBox);
                stack.Children.Add(varPanel);

                // Value Type
                var typePanel = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 5) };
                typePanel.Children.Add(new TextBlock { Text = "Value type:", Width = 120, VerticalAlignment = VerticalAlignment.Center });
                var valueTypeCombo = new ComboBox
                {
                    Width = 180,
                    ItemsSource = new[] { "int", "long", "float", "double", "bool", "string" },
                    SelectedItem = node.Configuration?.Properties.GetValueOrDefault("ValueType")?.ToString() ?? "int"
                };
                typePanel.Children.Add(valueTypeCombo);
                stack.Children.Add(typePanel);

                // Value
                var valuePanel = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 5) };
                valuePanel.Children.Add(new TextBlock { Text = "Value:", Width = 120, VerticalAlignment = VerticalAlignment.Center });
                var valueBox = new TextBox
                {
                    Width = 180,
                    Text = node.Configuration?.Properties.GetValueOrDefault("Value")?.ToString() ?? "0"
                };
                valuePanel.Children.Add(valueBox);
                stack.Children.Add(valuePanel);

                var saveBtn = new Button { Content = "Save", Margin = new Thickness(0, 10, 0, 0) };
                saveBtn.Click += (_, _) =>
                {
                    node.Configuration ??= new NodeConfiguration();
                    node.Configuration.Properties["VariableName"] = varNameBox.Text;
                    node.Configuration.Properties["ValueType"] = valueTypeCombo.SelectedItem?.ToString() ?? "int";
                    node.Configuration.Properties["Value"] = valueBox.Text;
                    Log($"SetVariableNode configured: {varNameBox.Text} = {valueBox.Text}");
                    dialog.Close();
                };
                stack.Children.Add(saveBtn);
            }
            else if (node.NodeType == typeof(GetVariableNode))
            {
                stack.Children.Add(new TextBlock
                {
                    Text = "Get and log a variable value",
                    FontWeight = FontWeights.Bold,
                    Margin = new Thickness(0, 0, 0, 10)
                });

                stack.Children.Add(new TextBlock { Text = "Variable name:", Margin = new Thickness(0, 0, 0, 5) });
                var varNameBox = new TextBox
                {
                    Width = 300,
                    Text = node.Configuration?.StringValue ?? "myVar"
                };
                stack.Children.Add(varNameBox);

                stack.Children.Add(new TextBlock
                {
                    Text = "This node will read the variable and log its value.\nUseful for debugging.",
                    FontSize = 11,
                    Foreground = Brushes.Gray,
                    Margin = new Thickness(0, 10, 0, 0),
                    TextWrapping = TextWrapping.Wrap
                });

                var saveBtn = new Button { Content = "Save", Margin = new Thickness(0, 10, 0, 0) };
                saveBtn.Click += (_, _) =>
                {
                    node.Configuration ??= new NodeConfiguration();
                    node.Configuration.StringValue = varNameBox.Text;
                    Log($"GetVariableNode configured: {varNameBox.Text}");
                    dialog.Close();
                };
                stack.Children.Add(saveBtn);
            }
            else
            {
                stack.Children.Add(new TextBlock
                {
                    Text = "This node type doesn't have configurable properties yet.",
                    TextWrapping = TextWrapping.Wrap
                });
            }

            dialog.Content = stack;
            dialog.ShowDialog();
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
            else if (t.Name.Contains("Loop") || t.Name.Contains("Repeat") || t == typeof(WaitForPacketNode))
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

        private ActionGraph BuildActionGraph()
        {
            var graph = new ActionGraph { Name = "Bot Action Graph" };
            var nodeMapping = new Dictionary<NodeVisual, ActionNodeBase>();

            foreach (var visual in allNodes)
            {
                try
                {
                    ActionNodeBase? actionNode = null;
                    if (visual.IsRoot)
                    {
                        actionNode = visual.ActionNode!;
                    }
                    else if (visual.NodeType == typeof(SendPacketNode))
                    {
                        var packetId = visual.Configuration?.PacketId ?? PacketId.INVALID_PACKET_ID;
                        if (packetId == PacketId.INVALID_PACKET_ID)
                        {
                            throw new InvalidOperationException(
                                $"SendPacketNode '{visual.NodeType.Name}' requires a valid PacketId. " +
                                "Please double-click the node to configure it.");
                        }

                        actionNode = new SendPacketNode
                        {
                            Name = visual.NodeType.Name,
                            PacketId = visual.Configuration?.PacketId ?? PacketId.INVALID_PACKET_ID,
                            PacketBuilder = (_) =>
                            {
                                var buffer = new NetBuffer();
                                return buffer;
                            }
                        };
                    }
                    else if (visual.NodeType == typeof(DelayNode))
                    {
                        actionNode = new DelayNode
                        {
                            Name = visual.NodeType.Name,
                            DelayMilliseconds = visual.Configuration?.IntValue ?? 1000
                        };
                    }
                    else if (visual.NodeType == typeof(CustomActionNode))
                    {
                        actionNode = new CustomActionNode
                        {
                            Name = visual.NodeType.Name,
                            ActionHandler = (_, _) =>
                            {
                                Serilog.Log.Information($"Custom action: {visual.NodeType.Name}");
                            }
                        };
                    }
                    else if (visual.NodeType == typeof(ConditionalNode))
                    {
                        var leftType = visual.Configuration?.Properties.GetValueOrDefault("LeftType")?.ToString();
                        var left = visual.Configuration?.Properties.GetValueOrDefault("Left")?.ToString();
                        var op = visual.Configuration?.Properties.GetValueOrDefault("Op")?.ToString();
                        var rightType = visual.Configuration?.Properties.GetValueOrDefault("RightType")?.ToString();
                        var right = visual.Configuration?.Properties.GetValueOrDefault("Right")?.ToString();

                        if (left != null && op != null && right != null)
                        {
                            actionNode = new ConditionalNode
                            {
                                Name = visual.NodeType.Name,
                                Condition = ctx => EvaluateConditionWithAccessors(ctx, leftType, left, op, rightType, right)
                            };
                        }
                    }
                    else if (visual.NodeType == typeof(PacketParserNode))
                    {
                        var setterMethod = visual.Configuration?.Properties.GetValueOrDefault("SetterMethod")?.ToString();

                        if (!string.IsNullOrEmpty(setterMethod))
                        {
                            actionNode = new PacketParserNode
                            {
                                Name = visual.NodeType.Name,
                                SetterMethodName = setterMethod
                            };
                        }
                    }
                    else if (visual.NodeType == typeof(LoopNode))
                    {
                        var count = visual.Configuration?.Properties.GetValueOrDefault("LoopCount") as int? ?? 1;

                        actionNode = new LoopNode
                        {
                            Name = visual.NodeType.Name,
                            ContinueCondition = ctx =>
                            {
                                var i = ctx.GetOrDefault("LoopIndex", 0) + 1;
                                ctx.Set("LoopIndex", i);
                                return i < count;
                            },
                            MaxIterations = count
                        };
                    }
                    else if (visual.NodeType == typeof(RepeatTimerNode))
                    {
                        actionNode = new RepeatTimerNode
                        {
                            Name = visual.NodeType.Name,
                            IntervalMilliseconds = visual.Configuration?.IntValue ?? 1000,
                            RepeatCount = visual.Configuration?.Properties.ContainsKey("RepeatCount") == true
                                ? (int)visual.Configuration.Properties["RepeatCount"]
                                : 10
                        };
                    }
                    else if (visual.NodeType == typeof(LogNode))
                    {
                        actionNode = new LogNode
                        {
                            Name = visual.NodeType.Name,
                            MessageBuilder = (_, _) => $"Log from {visual.NodeType.Name}"
                        };
                    }
                    else if (visual.NodeType != null)
                    {
                        actionNode = new CustomActionNode
                        {
                            Name = visual.NodeType.Name,
                            ActionHandler = (_, _) =>
                            {
                                Serilog.Log.Information($"Executing node: {visual.NodeType.Name}");
                            }
                        };
                    }
                    else if (visual.NodeType == typeof(DisconnectNode))
                    {
                        actionNode = new DisconnectNode
                        {
                            Name = visual.NodeType.Name,
                            Reason = visual.Configuration?.StringValue ?? "User requested disconnect"
                        };
                    }
                    else if (visual.NodeType == typeof(RandomDelayNode))
                    {
                        var minDelay = visual.Configuration?.Properties.GetValueOrDefault("MinDelay") as int? ?? 500;
                        var maxDelay = visual.Configuration?.Properties.GetValueOrDefault("MaxDelay") as int? ?? 2000;

                        actionNode = new RandomDelayNode
                        {
                            Name = visual.NodeType.Name,
                            MinDelayMilliseconds = minDelay,
                            MaxDelayMilliseconds = maxDelay
                        };
                    }
                    else if (visual.NodeType == typeof(WaitForPacketNode))
                    {
                        var packetId = visual.Configuration?.PacketId ?? PacketId.INVALID_PACKET_ID;
                        var timeout = visual.Configuration?.IntValue ?? 5000;

                        if (packetId == PacketId.INVALID_PACKET_ID)
                        {
                            throw new InvalidOperationException(
                                "WaitForPacketNode requires a valid PacketId. " +
                                "Please double-click the node to configure it.");
                        }

                        actionNode = new WaitForPacketNode
                        {
                            Name = visual.NodeType.Name,
                            ExpectedPacketId = packetId,
                            TimeoutMilliseconds = timeout
                        };
                    }
                    else if (visual.NodeType == typeof(SetVariableNode))
                    {
                        var variableName = visual.Configuration?.Properties.GetValueOrDefault("VariableName")?.ToString() ?? "value";
                        var valueType = visual.Configuration?.Properties.GetValueOrDefault("ValueType")?.ToString() ?? "int";
                        var value = visual.Configuration?.Properties.GetValueOrDefault("Value")?.ToString() ?? "0";

                        actionNode = new SetVariableNode
                        {
                            Name = visual.NodeType.Name,
                            VariableName = variableName,
                            ValueType = valueType,
                            StringValue = value
                        };
                    }
                    else if (visual.NodeType == typeof(GetVariableNode))
                    {
                        var variableName = visual.Configuration?.StringValue ?? "value";

                        actionNode = new GetVariableNode
                        {
                            Name = visual.NodeType.Name,
                            VariableName = variableName
                        };
                    }

                    if (actionNode is WaitForPacketNode waitNode)
                    {
                        if (visual.FalseChild != null)
                        {
                            if (nodeMapping.TryGetValue(visual.FalseChild, out var timeoutNode))
                            {
                                waitNode.TimeoutNodes.Add(timeoutNode);
                            }
                            else
                            {
                                Serilog.Log.Warning($"Timeout node not found in mapping for {actionNode.Name}");
                            }
                        }

                        if (visual.TrueChild != null)
                        {
                            if (nodeMapping.TryGetValue(visual.TrueChild, out var successNode))
                            {
                                actionNode.NextNodes.Add(successNode);
                            }
                            else
                            {
                                Serilog.Log.Warning($"Success node not found in mapping for {actionNode.Name}");
                            }
                        }
                    }
                    else
                    {
                        Serilog.Log.Warning("Node has no type information, skipping");
                        continue;
                    }

                    if (actionNode == null)
                    {
                        Serilog.Log.Warning($"Node {visual.NodeType?.Name} could not be created, skipping");
                        continue;
                    }

                    nodeMapping[visual] = actionNode;
                    visual.ActionNode = actionNode;
                }
                catch (Exception ex)
                {
                    Serilog.Log.Error($"Error creating action node for {visual.NodeType?.Name}: {ex.Message}");
                    throw new Exception($"Failed to create node '{visual.NodeType?.Name}': {ex.Message}");
                }
            }

            foreach (var visual in allNodes)
            {
                if (!nodeMapping.TryGetValue(visual, out var actionNode))
                {
                    Serilog.Log.Warning("Visual node not in mapping, skipping connections");
                    continue;
                }

                if (visual.Next != null)
                {
                    if (nodeMapping.TryGetValue(visual.Next, out var value))
                    {
                        actionNode.NextNodes.Add(value);
                    }
                    else
                    {
                        Serilog.Log.Warning($"Next node not found in mapping for {actionNode.Name}");
                    }
                }

                if (actionNode is ConditionalNode conditionalNode)
                {
                    if (visual.TrueChild != null)
                    {
                        if (nodeMapping.TryGetValue(visual.TrueChild, out var value))
                        {
                            conditionalNode.TrueNodes.Add(value);
                        }
                        else
                        {
                            Serilog.Log.Warning($"TrueChild not found in mapping for {actionNode.Name}");
                        }
                    }

                    if (visual.FalseChild != null)
                    {
                        if (nodeMapping.TryGetValue(visual.FalseChild, out var value))
                        {
                            conditionalNode.FalseNodes.Add(value);
                        }
                        else
                        {
                            Serilog.Log.Warning($"FalseChild not found in mapping for {actionNode.Name}");
                        }
                    }
                }

                if (actionNode is LoopNode loopNode)
                {
                    if (visual.TrueChild != null)
                    {
                        if (nodeMapping.TryGetValue(visual.TrueChild, out var value))
                        {
                            loopNode.LoopBody.Add(value);
                        }
                        else
                        {
                            Serilog.Log.Warning($"LoopBody child not found in mapping for {actionNode.Name}");
                        }
                    }

                    if (visual.FalseChild != null)
                    {
                        if (nodeMapping.TryGetValue(visual.FalseChild, out var value))
                        {
                            loopNode.ExitNodes.Add(value);
                        }
                        else
                        {
                            Serilog.Log.Warning($"ExitNode child not found in mapping for {actionNode.Name}");
                        }
                    }
                }

                if (actionNode is RepeatTimerNode repeatNode)
                {
                    if (visual.TrueChild != null)
                    {
                        if (nodeMapping.TryGetValue(visual.TrueChild, out var value))
                        {
                            repeatNode.RepeatBody.Add(value);
                        }
                        else
                        {
                            Serilog.Log.Warning($"RepeatBody child not found in mapping for {actionNode.Name}");
                        }
                    }
                }

                graph.AddNode(actionNode);
            }

            Serilog.Log.Information($"Graph built with {nodeMapping.Count} nodes");
            return graph;
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
