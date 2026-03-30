using MultiSocketRUDPBotTester.Bot;
using System.Windows;
using System.Windows.Controls;
using static System.Enum;

namespace MultiSocketRUDPBotTester.UI
{
    public interface INodeConfigPanel
    {
        bool CanConfigure(NodeVisual node);
        void Build(StackPanel stack, NodeVisual node, Action<string> log, Action closeDialog);
    }

    public class SendPacketConfigPanel : INodeConfigPanel
    {
        public bool CanConfigure(NodeVisual node) => node.NodeType == typeof(SendPacketNode);

        public void Build(StackPanel stack, NodeVisual node, Action<string> log, Action closeDialog)
        {
            stack.Children.Add(ConfigUi.Header("Send Packet"));
            stack.Children.Add(new TextBlock { Text = "Packet ID:", Margin = new Thickness(0, 0, 0, 4) });

            var combo = new ComboBox
            {
                ItemsSource = GetValues(typeof(PacketId)),
                SelectedItem = node.Configuration?.PacketId ?? PacketId.InvalidPacketId
            };
            stack.Children.Add(combo);

            var fieldsPanel = new StackPanel { Margin = new Thickness(0, 8, 0, 0) };
            stack.Children.Add(fieldsPanel);

            var fieldInputs = new Dictionary<string, TextBox>();
            void RefreshFields(PacketId packetId)
            {
                fieldsPanel.Children.Clear();
                fieldInputs.Clear();

                var schema = PacketSchema.Get(packetId);
                if (schema == null || schema.Length == 0)
                {
                    return;
                }

                fieldsPanel.Children.Add(new TextBlock
                {
                    Text = "Packet Fields:",
                    FontWeight = FontWeights.Bold,
                    Margin = new Thickness(0, 0, 0, 4)
                });

                foreach (var field in schema)
                {
                    var saved = node.Configuration?.Properties.GetValueOrDefault($"Field_{field.Name}");
                    var box = ConfigUi.LabeledBox(fieldsPanel,
                        $"{field.Name} ({field.Type}):",
                        saved?.ToString() ?? field.DefaultValue?.ToString() ?? "",
                        labelWidth: 160);
                    fieldInputs[field.Name] = box;
                }
            }

            RefreshFields((PacketId)combo.SelectedItem);
            combo.SelectionChanged += (_, _) => RefreshFields((PacketId)combo.SelectedItem);

            stack.Children.Add(ConfigUi.SaveButton(() =>
            {
                node.Configuration ??= new NodeConfiguration();
                node.Configuration.PacketId = (PacketId)combo.SelectedItem;

                foreach (var (name, box) in fieldInputs)
                {
                    node.Configuration.Properties[$"Field_{name}"] = box.Text;
                }

                log($"SendPacketNode configured: {combo.SelectedItem}");
                closeDialog();
            }));
        }
    }

    public class DelayConfigPanel : INodeConfigPanel
    {
        public bool CanConfigure(NodeVisual node) => node.NodeType == typeof(DelayNode);

        public void Build(StackPanel stack, NodeVisual node, Action<string> log, Action closeDialog)
        {
            stack.Children.Add(ConfigUi.Header("Delay Node"));
            var box = ConfigUi.LabeledBox(stack, "Delay (ms):",
                node.Configuration?.IntValue.ToString() ?? "1000");

            stack.Children.Add(ConfigUi.SaveButton(() =>
            {
                if (!int.TryParse(box.Text, out var ms))
                {
                    MessageBox.Show("Invalid delay value.", "Error",
                        MessageBoxButton.OK, MessageBoxImage.Error);
                    return;
                }
                node.Configuration ??= new NodeConfiguration();
                node.Configuration.IntValue = ms;
                log($"DelayNode configured: {ms}ms");
                closeDialog();
            }));
        }
    }

    public class RandomDelayConfigPanel : INodeConfigPanel
    {
        public bool CanConfigure(NodeVisual node) => node.NodeType == typeof(RandomDelayNode);

        public void Build(StackPanel stack, NodeVisual node, Action<string> log, Action closeDialog)
        {
            stack.Children.Add(ConfigUi.Header("Random Delay Configuration"));

            var minBox = ConfigUi.LabeledBox(stack, "Min Delay (ms):",
                node.Configuration?.Properties.GetValueOrDefault("MinDelay")?.ToString() ?? "500");
            var maxBox = ConfigUi.LabeledBox(stack, "Max Delay (ms):",
                node.Configuration?.Properties.GetValueOrDefault("MaxDelay")?.ToString() ?? "2000");

            stack.Children.Add(ConfigUi.Hint("Delays randomly between min and max milliseconds.\nUseful for simulating human-like behavior."));

            stack.Children.Add(ConfigUi.SaveButton(() =>
            {
                if (!int.TryParse(minBox.Text, out var min) || !int.TryParse(maxBox.Text, out var max))
                {
                    MessageBox.Show("Invalid delay values.", "Error",
                        MessageBoxButton.OK, MessageBoxImage.Error);
                    return;
                }
                if (min > max)
                {
                    MessageBox.Show("Min delay cannot be greater than max delay.", "Error",
                        MessageBoxButton.OK, MessageBoxImage.Error);
                    return;
                }
                node.Configuration ??= new NodeConfiguration();
                node.Configuration.Properties["MinDelay"] = min;
                node.Configuration.Properties["MaxDelay"] = max;
                log($"RandomDelayNode configured: {min}-{max}ms");
                closeDialog();
            }));
        }
    }

    public class LogConfigPanel : INodeConfigPanel
    {
        public bool CanConfigure(NodeVisual node) => node.NodeType == typeof(LogNode);

        public void Build(StackPanel stack, NodeVisual node, Action<string> log, Action closeDialog)
        {
            stack.Children.Add(ConfigUi.Header("Log Message"));
            stack.Children.Add(ConfigUi.Hint("Available placeholders: {sessionId}, {isConnected}, {packetSize}"));

            var msgBox = new TextBox
            {
                Height = 120,
                AcceptsReturn = true,
                TextWrapping = TextWrapping.Wrap,
                VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
                Margin = new Thickness(0, 6, 0, 0),
                Text = node.Configuration?.StringValue ?? ""
            };
            stack.Children.Add(msgBox);

            stack.Children.Add(ConfigUi.SaveButton(() =>
            {
                var msg = msgBox.Text.Trim();
                if (string.IsNullOrEmpty(msg))
                {
                    MessageBox.Show("Log message cannot be empty.", "Warning",
                        MessageBoxButton.OK, MessageBoxImage.Warning);
                    return;
                }
                node.Configuration ??= new NodeConfiguration();
                node.Configuration.StringValue = msg;
                log($"LogNode configured: {msg}");
                closeDialog();
            }));
        }
    }

    public class DisconnectConfigPanel : INodeConfigPanel
    {
        public bool CanConfigure(NodeVisual node) => node.NodeType == typeof(DisconnectNode);

        public void Build(StackPanel stack, NodeVisual node, Action<string> log, Action closeDialog)
        {
            stack.Children.Add(ConfigUi.Header("Disconnect Client"));
            stack.Children.Add(new TextBlock { Text = "Reason:", Margin = new Thickness(0, 0, 0, 4) });

            var reasonBox = new TextBox
            {
                Height = 60,
                AcceptsReturn = true,
                TextWrapping = TextWrapping.Wrap,
                Text = node.Configuration?.StringValue ?? "User requested disconnect"
            };
            stack.Children.Add(reasonBox);
            stack.Children.Add(ConfigUi.Hint("This node will gracefully disconnect the client."));

            stack.Children.Add(ConfigUi.SaveButton(() =>
            {
                node.Configuration ??= new NodeConfiguration();
                node.Configuration.StringValue = reasonBox.Text;
                log($"DisconnectNode configured: {reasonBox.Text}");
                closeDialog();
            }));
        }
    }

    public class WaitForPacketConfigPanel : INodeConfigPanel
    {
        public bool CanConfigure(NodeVisual node) => node.NodeType == typeof(WaitForPacketNode);

        public void Build(StackPanel stack, NodeVisual node, Action<string> log, Action closeDialog)
        {
            stack.Children.Add(ConfigUi.Header("Wait For Packet Configuration"));
            stack.Children.Add(new TextBlock { Text = "Expected Packet ID:", Margin = new Thickness(0, 0, 0, 4) });

            var combo = new ComboBox
            {
                Width = 300,
                ItemsSource = GetValues(typeof(PacketId)),
                SelectedItem = node.Configuration?.PacketId ?? PacketId.InvalidPacketId
            };
            stack.Children.Add(combo);

            var timeoutBox = ConfigUi.LabeledBox(stack, "Timeout (ms):",
                node.Configuration?.IntValue.ToString() ?? "5000");

            stack.Children.Add(ConfigUi.Hint(
                "Waits until the specified packet is received or timeout occurs.\n" +
                "• 'continue' port → success path\n" +
                "• 'exit' port     → timeout path"));

            stack.Children.Add(ConfigUi.SaveButton(() =>
            {
                if (!int.TryParse(timeoutBox.Text, out var timeout))
                {
                    MessageBox.Show("Invalid timeout value.", "Error",
                        MessageBoxButton.OK, MessageBoxImage.Error);
                    return;
                }
                node.Configuration ??= new NodeConfiguration();
                node.Configuration.PacketId = (PacketId)combo.SelectedItem;
                node.Configuration.IntValue = timeout;
                log($"WaitForPacketNode configured: {combo.SelectedItem}, timeout={timeout}ms");
                closeDialog();
            }));
        }
    }

    public class SetVariableConfigPanel : INodeConfigPanel
    {
        public bool CanConfigure(NodeVisual node) => node.NodeType == typeof(SetVariableNode);

        public void Build(StackPanel stack, NodeVisual node, Action<string> log, Action closeDialog)
        {
            stack.Children.Add(ConfigUi.Header("Set Variable"));

            var nameBox = ConfigUi.LabeledBox(stack, "Variable name:",
                node.Configuration?.Properties.GetValueOrDefault("VariableName")?.ToString() ?? "myVar");

            stack.Children.Add(new TextBlock { Text = "Value type:", Margin = new Thickness(0, 0, 0, 3) });
            var typeCombo = new ComboBox
            {
                Width = 180,
                ItemsSource = new[] { "int", "long", "float", "double", "bool", "string" },
                SelectedItem = node.Configuration?.Properties.GetValueOrDefault("ValueType")?.ToString() ?? "int"
            };
            stack.Children.Add(typeCombo);

            var valueBox = ConfigUi.LabeledBox(stack, "Value:",
                node.Configuration?.Properties.GetValueOrDefault("Value")?.ToString() ?? "0");

            stack.Children.Add(ConfigUi.SaveButton(() =>
            {
                node.Configuration ??= new NodeConfiguration();
                node.Configuration.Properties["VariableName"] = nameBox.Text;
                node.Configuration.Properties["ValueType"] = typeCombo.SelectedItem?.ToString() ?? "int";
                node.Configuration.Properties["Value"] = valueBox.Text;
                log($"SetVariableNode configured: {nameBox.Text} = {valueBox.Text}");
                closeDialog();
            }));
        }
    }

    public class GetVariableConfigPanel : INodeConfigPanel
    {
        public bool CanConfigure(NodeVisual node) => node.NodeType == typeof(GetVariableNode);

        public void Build(StackPanel stack, NodeVisual node, Action<string> log, Action closeDialog)
        {
            stack.Children.Add(ConfigUi.Header("Get Variable"));
            stack.Children.Add(new TextBlock { Text = "Variable name:", Margin = new Thickness(0, 0, 0, 4) });

            var nameBox = new TextBox
            {
                Width = 300,
                Text = node.Configuration?.StringValue ?? "myVar"
            };
            stack.Children.Add(nameBox);
            stack.Children.Add(ConfigUi.Hint("Reads the variable from context and logs its value.\nUseful for debugging."));

            stack.Children.Add(ConfigUi.SaveButton(() =>
            {
                node.Configuration ??= new NodeConfiguration();
                node.Configuration.StringValue = nameBox.Text;
                log($"GetVariableNode configured: {nameBox.Text}");
                closeDialog();
            }));
        }
    }

    public class LoopConfigPanel : INodeConfigPanel
    {
        public bool CanConfigure(NodeVisual node) => node.NodeType == typeof(LoopNode);

        public void Build(StackPanel stack, NodeVisual node, Action<string> log, Action closeDialog)
        {
            stack.Children.Add(ConfigUi.Header("Loop Node"));
            var countBox = ConfigUi.LabeledBox(stack, "Loop Count:",
                node.Configuration?.Properties.GetValueOrDefault("LoopCount")?.ToString() ?? "5");

            stack.Children.Add(ConfigUi.Hint("• 'continue' port → loop body\n• 'exit' port → after loop"));

            stack.Children.Add(ConfigUi.SaveButton(() =>
            {
                if (!int.TryParse(countBox.Text, out var n) || n <= 0)
                {
                    MessageBox.Show("Loop count must be a positive integer.", "Error",
                        MessageBoxButton.OK, MessageBoxImage.Error);
                    return;
                }
                node.Configuration ??= new NodeConfiguration();
                node.Configuration.Properties["LoopCount"] = n;
                log($"LoopNode configured: {n} iterations");
                closeDialog();
            }));
        }
    }

    public class RepeatTimerConfigPanel : INodeConfigPanel
    {
        public bool CanConfigure(NodeVisual node) => node.NodeType == typeof(RepeatTimerNode);

        public void Build(StackPanel stack, NodeVisual node, Action<string> log, Action closeDialog)
        {
            stack.Children.Add(ConfigUi.Header("Repeat Timer Configuration"));

            var countBox = ConfigUi.LabeledBox(stack, "Repeat Count:",
                node.Configuration?.Properties.GetValueOrDefault("RepeatCount")?.ToString() ?? "10");
            var intervalBox = ConfigUi.LabeledBox(stack, "Interval (ms):",
                node.Configuration?.IntValue.ToString() ?? "1000");

            stack.Children.Add(ConfigUi.Hint(
                "Executes the connected action repeatedly at the given interval.\n" +
                "• 'continue' port → action to repeat\n" +
                "• After all iterations, proceeds to next nodes."));

            stack.Children.Add(ConfigUi.SaveButton(() =>
            {
                if (!int.TryParse(countBox.Text, out var count) ||
                    !int.TryParse(intervalBox.Text, out var interval))
                {
                    MessageBox.Show("Invalid count or interval value.", "Error",
                        MessageBoxButton.OK, MessageBoxImage.Error);
                    return;
                }
                if (count <= 0 || interval < 0)
                {
                    MessageBox.Show("Count must be positive and interval cannot be negative.", "Error",
                        MessageBoxButton.OK, MessageBoxImage.Error);
                    return;
                }
                node.Configuration ??= new NodeConfiguration();
                node.Configuration.Properties["RepeatCount"] = count;
                node.Configuration.IntValue = interval;
                log($"RepeatTimerNode configured: {count} times, {interval}ms interval");
                closeDialog();
            }));
        }
    }

    public class AssertConfigPanel : INodeConfigPanel
    {
        public bool CanConfigure(NodeVisual node) => node.NodeType == typeof(AssertNode);

        public void Build(StackPanel stack, NodeVisual node, Action<string> log, Action closeDialog)
        {
            stack.Children.Add(ConfigUi.Header("Assert Node Configuration"));
            stack.Children.Add(new TextBlock { Text = "Error Message:", Margin = new Thickness(0, 0, 0, 4) });

            var msgBox = new TextBox
            {
                Height = 60,
                AcceptsReturn = true,
                TextWrapping = TextWrapping.Wrap,
                Text = node.Configuration?.StringValue ?? "Assertion failed"
            };
            stack.Children.Add(msgBox);

            var stopCheck = new CheckBox
            {
                Content = "Stop execution on failure",
                IsChecked = node.Configuration?.Properties.GetValueOrDefault("StopOnFailure") as bool? ?? true,
                Margin = new Thickness(0, 8, 0, 0)
            };
            stack.Children.Add(stopCheck);
            stack.Children.Add(ConfigUi.Hint("• 'continue' port → success path\n• 'exit' port → failure path"));

            stack.Children.Add(ConfigUi.SaveButton(() =>
            {
                node.Configuration ??= new NodeConfiguration();
                node.Configuration.StringValue = msgBox.Text;
                node.Configuration.Properties["StopOnFailure"] = stopCheck.IsChecked ?? true;
                log($"AssertNode configured: {msgBox.Text}");
                closeDialog();
            }));
        }
    }

    public class RetryConfigPanel : INodeConfigPanel
    {
        public bool CanConfigure(NodeVisual node) => node.NodeType == typeof(RetryNode);

        public void Build(StackPanel stack, NodeVisual node, Action<string> log, Action closeDialog)
        {
            stack.Children.Add(ConfigUi.Header("Retry Node Configuration"));

            var maxBox = ConfigUi.LabeledBox(stack, "Max Retries:",
                node.Configuration?.Properties.GetValueOrDefault("MaxRetries")?.ToString() ?? "3");
            var delayBox = ConfigUi.LabeledBox(stack, "Retry Delay (ms):",
                node.Configuration?.Properties.GetValueOrDefault("RetryDelay")?.ToString() ?? "1000");

            var expCheck = new CheckBox
            {
                Content = "Use Exponential Backoff",
                IsChecked = node.Configuration?.Properties.GetValueOrDefault("ExponentialBackoff") as bool? ?? false,
                Margin = new Thickness(0, 8, 0, 0)
            };
            stack.Children.Add(expCheck);
            stack.Children.Add(ConfigUi.Hint(
                "• 'continue' port → action to retry\n" +
                "• 'exit' port     → action on final failure"));

            stack.Children.Add(ConfigUi.SaveButton(() =>
            {
                if (!int.TryParse(maxBox.Text, out var maxRetries) ||
                    !int.TryParse(delayBox.Text, out var delay))
                {
                    MessageBox.Show("Invalid retry configuration.", "Error",
                        MessageBoxButton.OK, MessageBoxImage.Error);
                    return;
                }
                node.Configuration ??= new NodeConfiguration();
                node.Configuration.Properties["MaxRetries"] = maxRetries;
                node.Configuration.Properties["RetryDelay"] = delay;
                node.Configuration.Properties["ExponentialBackoff"] = expCheck.IsChecked ?? false;
                log($"RetryNode configured: {maxRetries} retries, {delay}ms delay");
                closeDialog();
            }));
        }
    }

    public class PacketParserConfigPanel : INodeConfigPanel
    {
        public bool CanConfigure(NodeVisual node) => node.NodeType == typeof(PacketParserNode);

        public void Build(StackPanel stack, NodeVisual node, Action<string> log, Action closeDialog)
        {
            stack.Children.Add(ConfigUi.Header("Packet Parser Node"));
            stack.Children.Add(new TextBlock { Text = "Select Setter:", Margin = new Thickness(0, 0, 0, 4) });

            var setterCombo = new ComboBox
            {
                Width = 350,
                DisplayMemberPath = "DisplayName",
                SelectedValuePath = "Method.Name",
                ItemsSource = VariableAccessorRegistry.GetAllSetters()
            };

            var saved = node.Configuration?.Properties.GetValueOrDefault("SetterMethod")?.ToString();
            if (saved != null)
            {
                setterCombo.SelectedValue = saved;
            }

            stack.Children.Add(setterCombo);
            stack.Children.Add(ConfigUi.Hint("Reads data from the incoming packet and stores it via the selected setter."));

            stack.Children.Add(ConfigUi.SaveButton(() =>
            {
                node.Configuration ??= new NodeConfiguration();
                node.Configuration.Properties["SetterMethod"] = setterCombo.SelectedValue?.ToString() ?? "";
                log($"PacketParserNode configured with setter: {setterCombo.Text}");
                closeDialog();
            }));
        }
    }

    public class RandomChoiceConfigPanel(Action<NodeVisual, int> createDynamicPorts) : INodeConfigPanel
    {
        public bool CanConfigure(NodeVisual node) => node.NodeType == typeof(RandomChoiceNode);

        public void Build(StackPanel stack, NodeVisual node, Action<string> log, Action closeDialog)
        {
            stack.Children.Add(ConfigUi.Header("Random Choice Configuration"));
            stack.Children.Add(ConfigUi.Hint("Randomly selects one connected branch based on equal weights."));
            stack.Children.Add(new TextBlock { Text = "Number of Choices:", Margin = new Thickness(0, 8, 0, 4) });

            var countBox = new TextBox
            {
                Width = 300,
                Text = node.Configuration?.IntValue.ToString() ?? "2"
            };
            stack.Children.Add(countBox);

            stack.Children.Add(ConfigUi.SaveButton(() =>
            {
                if (!int.TryParse(countBox.Text, out var count) || count < 2)
                {
                    MessageBox.Show("Choice count must be 2 or more.", "Error",
                        MessageBoxButton.OK, MessageBoxImage.Error);
                    return;
                }
                node.Configuration ??= new NodeConfiguration();
                node.Configuration.IntValue = count;
                createDynamicPorts(node, count);
                log($"RandomChoiceNode configured: {count} choices");
                closeDialog();
            }));
        }
    }

    public class ConditionalConfigPanel : INodeConfigPanel
    {
        public bool CanConfigure(NodeVisual node) => node.NodeType == typeof(ConditionalNode);

        public void Build(StackPanel stack, NodeVisual node, Action<string> log, Action closeDialog)
        {
            stack.Children.Add(ConfigUi.Header("Conditional Expression"));
            stack.Children.Add(ConfigUi.Hint("Select a getter function or enter a constant for each side."));

            var (leftType, leftGetter, leftConst) = AddValueSelector(
                stack, "Left Value:", node.Configuration, "LeftType", "Left");

            var opRow = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 4, 0, 8) };
            opRow.Children.Add(new TextBlock { Text = "Operator:", Width = 80, VerticalAlignment = VerticalAlignment.Center });
            var opCombo = new ComboBox
            {
                Width = 220,
                ItemsSource = new[] { ">", "<", "==", ">=", "<=", "!=" },
                SelectedItem = node.Configuration?.Properties.GetValueOrDefault("Op")?.ToString() ?? ">"
            };
            opRow.Children.Add(opCombo);
            stack.Children.Add(opRow);

            var (rightType, rightGetter, rightConst) = AddValueSelector(
                stack, "Right Value:", node.Configuration, "RightType", "Right");

            stack.Children.Add(ConfigUi.SaveButton(() =>
            {
                node.Configuration ??= new NodeConfiguration();
                Apply(node.Configuration, "Left", leftType, leftGetter, leftConst);
                node.Configuration.Properties["Op"] = opCombo.SelectedItem?.ToString() ?? ">";
                Apply(node.Configuration, "Right", rightType, rightGetter, rightConst);
                log("ConditionalNode configured");
                closeDialog();
            }));
        }

        private static (ComboBox typeCombo, ComboBox getterCombo, TextBox constBox)
            AddValueSelector(StackPanel stack, string label,
                NodeConfiguration? cfg, string typeKey, string valueKey)
        {
            var panel = new StackPanel { Margin = new Thickness(0, 0, 0, 8) };
            panel.Children.Add(new TextBlock { Text = label, FontWeight = FontWeights.Bold });

            var typeRow = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 4, 0, 4) };
            typeRow.Children.Add(new TextBlock { Text = "Type:", Width = 80 });
            var typeCombo = new ComboBox
            {
                Width = 220,
                ItemsSource = new[] { "Getter Function", "Constant" },
                SelectedItem = cfg?.Properties.GetValueOrDefault(typeKey)?.ToString() ?? "Constant"
            };
            typeRow.Children.Add(typeCombo);
            panel.Children.Add(typeRow);

            var getterCombo = new ComboBox
            {
                Width = 300,
                DisplayMemberPath = "DisplayName",
                SelectedValuePath = "Method.Name",
                ItemsSource = VariableAccessorRegistry.GetAllGetters(),
                Visibility = Visibility.Collapsed
            };

            var constBox = new TextBox
            {
                Width = 300,
                Text = cfg?.Properties.GetValueOrDefault(valueKey)?.ToString() ?? "0"
            };

            typeCombo.SelectionChanged += (_, _) =>
            {
                var isGetter = typeCombo.SelectedItem?.ToString() == "Getter Function";
                getterCombo.Visibility = isGetter ? Visibility.Visible : Visibility.Collapsed;
                constBox.Visibility = isGetter ? Visibility.Collapsed : Visibility.Visible;
            };

            panel.Children.Add(getterCombo);
            panel.Children.Add(constBox);
            stack.Children.Add(panel);

            if (cfg?.Properties.GetValueOrDefault(typeKey)?.ToString() != "Getter Function")
            {
                return (typeCombo, getterCombo, constBox);
            }

            getterCombo.Visibility = Visibility.Visible;
            constBox.Visibility = Visibility.Collapsed;
            var saved = cfg.Properties.GetValueOrDefault(valueKey)?.ToString();
            if (saved != null)
            {
                getterCombo.SelectedValue = saved;
            }

            return (typeCombo, getterCombo, constBox);
        }

        private static void Apply(NodeConfiguration cfg, string baseKey,
            ComboBox typeCombo, ComboBox getterCombo, TextBox constBox)
        {
            var isGetter = typeCombo.SelectedItem?.ToString() == "Getter Function";
            cfg.Properties[$"{baseKey}Type"] = isGetter ? "Getter Function" : "Constant";
            cfg.Properties[baseKey] = isGetter
                ? getterCombo.SelectedValue?.ToString() ?? ""
                : constBox.Text;
        }
    }
}