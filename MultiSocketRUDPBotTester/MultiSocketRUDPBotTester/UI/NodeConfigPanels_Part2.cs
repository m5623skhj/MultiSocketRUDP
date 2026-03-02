using MultiSocketRUDPBotTester.Bot;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace MultiSocketRUDPBotTester.UI
{
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
                setterCombo.SelectedValue = saved;

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
                return (typeCombo, getterCombo, constBox);

            getterCombo.Visibility = Visibility.Visible;
            constBox.Visibility = Visibility.Collapsed;
            var saved = cfg.Properties.GetValueOrDefault(valueKey)?.ToString();
            if (saved != null)
                getterCombo.SelectedValue = saved;

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

    public class NodeConfigPanelRegistry(Action<NodeVisual, int> createDynamicPorts)
    {
        private readonly List<INodeConfigPanel> panels =
        [
            new SendPacketConfigPanel(),
            new DelayConfigPanel(),
            new RandomDelayConfigPanel(),
            new LogConfigPanel(),
            new DisconnectConfigPanel(),
            new WaitForPacketConfigPanel(),
            new SetVariableConfigPanel(),
            new GetVariableConfigPanel(),
            new LoopConfigPanel(),
            new RepeatTimerConfigPanel(),
            new AssertConfigPanel(),
            new RetryConfigPanel(),
            new PacketParserConfigPanel(),
            new RandomChoiceConfigPanel(createDynamicPorts),
            new ConditionalConfigPanel()
        ];

        public INodeConfigPanel? Find(NodeVisual node) =>
            panels.FirstOrDefault(p => p.CanConfigure(node));
    }
}