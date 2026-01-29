using MultiSocketRUDPBotTester.Bot;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows;
using static System.Enum;

namespace MultiSocketRUDPBotTester
{
    public partial class BotActionGraphWindow : Window
    {

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
                    ItemsSource = GetValues(typeof(PacketId)),
                    SelectedItem = node.Configuration?.PacketId ?? PacketId.InvalidPacketId
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

                stack.Children.Add(new TextBlock
                {
                    Text = "Available variables (use in your message):",
                    FontWeight = FontWeights.Bold,
                    FontSize = 10,
                    Foreground = Brushes.LightBlue,
                    Margin = new Thickness(0, 10, 0, 3)
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
                    ItemsSource = GetValues(typeof(PacketId)),
                    SelectedItem = node.Configuration?.PacketId ?? PacketId.InvalidPacketId
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

                var varPanel = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 5) };
                varPanel.Children.Add(new TextBlock { Text = "Variable name:", Width = 120, VerticalAlignment = VerticalAlignment.Center });
                var varNameBox = new TextBox
                {
                    Width = 180,
                    Text = node.Configuration?.Properties.GetValueOrDefault("VariableName")?.ToString() ?? "myVar"
                };
                varPanel.Children.Add(varNameBox);
                stack.Children.Add(varPanel);

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
            else if (node.NodeType == typeof(RandomChoiceNode))
            {
                stack.Children.Add(new TextBlock
                {
                    Text = "Random Choice Configuration",
                    FontWeight = FontWeights.Bold,
                    Margin = new Thickness(0, 0, 0, 10)
                });

                stack.Children.Add(new TextBlock
                {
                    Text = "This node randomly selects one of the connected branches based on weights.\n" +
                           "Connect branches using output ports, then set weights here.",
                    FontSize = 11,
                    Foreground = Brushes.Gray,
                    Margin = new Thickness(0, 0, 0, 10),
                    TextWrapping = TextWrapping.Wrap
                });

                stack.Children.Add(new TextBlock { Text = "Number of Choices:", Margin = new Thickness(0, 0, 0, 5) });
                var choiceCountBox = new TextBox
                {
                    Width = 300,
                    Text = node.Configuration?.IntValue.ToString() ?? "2"
                };
                stack.Children.Add(choiceCountBox);

                stack.Children.Add(new TextBlock
                {
                    Text = "Note: After building the graph, branches will be randomly selected based on equal weights.",
                    FontSize = 10,
                    Foreground = Brushes.Orange,
                    Margin = new Thickness(0, 10, 0, 0),
                    TextWrapping = TextWrapping.Wrap
                });

                var saveBtn = new Button { Content = "Save", Margin = new Thickness(0, 10, 0, 0) };
                saveBtn.Click += (_, _) =>
                {
                    if (int.TryParse(choiceCountBox.Text, out var count) && count > 0)
                    {
                        node.Configuration ??= new NodeConfiguration();
                        node.Configuration.IntValue = count;

                        CreateDynamicPorts(node, count);

                        Log($"RandomChoiceNode configured: {count} choices");
                        dialog.Close();
                    }
                    else
                    {
                        MessageBox.Show("Invalid choice count", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                    }
                };
                stack.Children.Add(saveBtn);
            }
            else if (node.NodeType == typeof(AssertNode))
            {
                stack.Children.Add(new TextBlock
                {
                    Text = "Assert Node Configuration",
                    FontWeight = FontWeights.Bold,
                    Margin = new Thickness(0, 0, 0, 10)
                });

                stack.Children.Add(new TextBlock { Text = "Error Message:", Margin = new Thickness(0, 0, 0, 5) });
                var messageBox = new TextBox
                {
                    Width = 300,
                    Height = 60,
                    AcceptsReturn = true,
                    TextWrapping = TextWrapping.Wrap,
                    Text = node.Configuration?.StringValue ?? "Assertion failed"
                };
                stack.Children.Add(messageBox);

                var stopOnFailureCheck = new CheckBox
                {
                    Content = "Stop execution on failure",
                    IsChecked = node.Configuration?.Properties.GetValueOrDefault("StopOnFailure") as bool? ?? true,
                    Margin = new Thickness(0, 10, 0, 0)
                };
                stack.Children.Add(stopOnFailureCheck);

                stack.Children.Add(new TextBlock
                {
                    Text = "Configure the condition in the ConditionalNode style.\n" +
                           "Use 'continue' output for success path.\n" +
                           "Use 'exit' output for failure path.",
                    FontSize = 11,
                    Foreground = Brushes.Gray,
                    Margin = new Thickness(0, 10, 0, 0),
                    TextWrapping = TextWrapping.Wrap
                });

                var saveBtn = new Button { Content = "Save", Margin = new Thickness(0, 10, 0, 0) };
                saveBtn.Click += (_, _) =>
                {
                    node.Configuration ??= new NodeConfiguration();
                    node.Configuration.StringValue = messageBox.Text;
                    node.Configuration.Properties["StopOnFailure"] = stopOnFailureCheck.IsChecked ?? true;
                    Log($"AssertNode configured: {messageBox.Text}");
                    dialog.Close();
                };
                stack.Children.Add(saveBtn);
            }
            else if (node.NodeType == typeof(RetryNode))
            {
                stack.Children.Add(new TextBlock
                {
                    Text = "Retry Node Configuration",
                    FontWeight = FontWeights.Bold,
                    Margin = new Thickness(0, 0, 0, 10)
                });

                var maxRetriesPanel = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 5) };
                maxRetriesPanel.Children.Add(new TextBlock { Text = "Max Retries:", Width = 120, VerticalAlignment = VerticalAlignment.Center });
                var maxRetriesBox = new TextBox
                {
                    Width = 180,
                    Text = node.Configuration?.Properties.GetValueOrDefault("MaxRetries")?.ToString() ?? "3"
                };
                maxRetriesPanel.Children.Add(maxRetriesBox);
                stack.Children.Add(maxRetriesPanel);

                var delayPanel = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 5) };
                delayPanel.Children.Add(new TextBlock { Text = "Retry Delay (ms):", Width = 120, VerticalAlignment = VerticalAlignment.Center });
                var delayBox = new TextBox
                {
                    Width = 180,
                    Text = node.Configuration?.Properties.GetValueOrDefault("RetryDelay")?.ToString() ?? "1000"
                };
                delayPanel.Children.Add(delayBox);
                stack.Children.Add(delayPanel);

                var exponentialCheck = new CheckBox
                {
                    Content = "Use Exponential Backoff",
                    IsChecked = node.Configuration?.Properties.GetValueOrDefault("ExponentialBackoff") as bool? ?? false,
                    Margin = new Thickness(0, 10, 0, 0)
                };
                stack.Children.Add(exponentialCheck);

                stack.Children.Add(new TextBlock
                {
                    Text = "Connect 'continue' port to the action to retry.\n" +
                           "Connect 'exit' port to the action on final failure.\n" +
                           "Success condition can be configured separately.",
                    FontSize = 11,
                    Foreground = Brushes.Gray,
                    Margin = new Thickness(0, 10, 0, 0),
                    TextWrapping = TextWrapping.Wrap
                });

                var saveBtn = new Button { Content = "Save", Margin = new Thickness(0, 10, 0, 0) };
                saveBtn.Click += (_, _) =>
                {
                    if (int.TryParse(maxRetriesBox.Text, out var maxRetries) &&
                        int.TryParse(delayBox.Text, out var delay))
                    {
                        node.Configuration ??= new NodeConfiguration();
                        node.Configuration.Properties["MaxRetries"] = maxRetries;
                        node.Configuration.Properties["RetryDelay"] = delay;
                        node.Configuration.Properties["ExponentialBackoff"] = exponentialCheck.IsChecked ?? false;
                        Log($"RetryNode configured: {maxRetries} retries, {delay}ms delay");
                        dialog.Close();
                    }
                    else
                    {
                        MessageBox.Show("Invalid retry configuration", "Error",
                            MessageBoxButton.OK, MessageBoxImage.Error);
                    }
                };
                stack.Children.Add(saveBtn);
            }
            else if (node.NodeType == typeof(RepeatTimerNode))
            {
                stack.Children.Add(new TextBlock
                {
                    Text = "Repeat Timer Configuration",
                    FontWeight = FontWeights.Bold,
                    Margin = new Thickness(0, 0, 0, 10)
                });

                var countPanel = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 5) };
                countPanel.Children.Add(new TextBlock
                {
                    Text = "Repeat Count:",
                    Width = 120,
                    VerticalAlignment = VerticalAlignment.Center
                });
                var countBox = new TextBox
                {
                    Width = 180,
                    Text = node.Configuration?.Properties.GetValueOrDefault("RepeatCount")?.ToString() ?? "10"
                };
                countPanel.Children.Add(countBox);
                stack.Children.Add(countPanel);

                var intervalPanel = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 5) };
                intervalPanel.Children.Add(new TextBlock
                {
                    Text = "Interval (ms):",
                    Width = 120,
                    VerticalAlignment = VerticalAlignment.Center
                });
                var intervalBox = new TextBox
                {
                    Width = 180,
                    Text = node.Configuration?.IntValue.ToString() ?? "1000"
                };
                intervalPanel.Children.Add(intervalBox);
                stack.Children.Add(intervalPanel);

                stack.Children.Add(new TextBlock
                {
                    Text = "Executes the connected action repeatedly with specified interval.\n" +
                           "Use 'continue' output to connect the action to repeat.\n" +
                           "After all iterations complete, proceeds to next nodes.",
                    FontSize = 11,
                    Foreground = Brushes.Gray,
                    Margin = new Thickness(0, 10, 0, 0),
                    TextWrapping = TextWrapping.Wrap
                });

                var saveBtn = new Button { Content = "Save", Margin = new Thickness(0, 10, 0, 0) };
                saveBtn.Click += (_, _) =>
                {
                    if (int.TryParse(countBox.Text, out var count) &&
                        int.TryParse(intervalBox.Text, out var interval))
                    {
                        if (count <= 0 || interval < 0)
                        {
                            MessageBox.Show("Count must be positive and interval cannot be negative",
                                "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                            return;
                        }

                        node.Configuration ??= new NodeConfiguration();
                        node.Configuration.Properties["RepeatCount"] = count;
                        node.Configuration.IntValue = interval;

                        Log($"RepeatTimerNode configured: {count} times, {interval}ms interval");
                        dialog.Close();
                    }
                    else
                    {
                        MessageBox.Show("Invalid count or interval value", "Error",
                            MessageBoxButton.OK, MessageBoxImage.Error);
                    }
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
    }
}
