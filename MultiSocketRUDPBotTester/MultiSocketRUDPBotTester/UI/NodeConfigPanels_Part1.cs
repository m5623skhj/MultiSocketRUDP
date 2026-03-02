using MultiSocketRUDPBotTester.Bot;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using static System.Enum;

namespace MultiSocketRUDPBotTester.UI
{
    public interface INodeConfigPanel
    {
        bool CanConfigure(NodeVisual node);
        void Build(StackPanel stack, NodeVisual node, Action<string> log, Action closeDialog);
    }

    internal static class ConfigUi
    {
        public static TextBlock Header(string text) => new()
        {
            Text = text,
            FontWeight = FontWeights.Bold,
            Margin = new Thickness(0, 0, 0, 10)
        };

        public static TextBlock Hint(string text) => new()
        {
            Text = text,
            FontSize = 11,
            Foreground = Brushes.Gray,
            TextWrapping = TextWrapping.Wrap,
            Margin = new Thickness(0, 8, 0, 0)
        };

        public static TextBox LabeledBox(StackPanel stack, string label, string value,
            double labelWidth = 120, double boxWidth = 180)
        {
            var row = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 5) };
            row.Children.Add(new TextBlock { Text = label, Width = labelWidth, VerticalAlignment = VerticalAlignment.Center });
            var box = new TextBox { Width = boxWidth, Text = value };
            row.Children.Add(box);
            stack.Children.Add(row);
            return box;
        }

        public static Button SaveButton(Action onClick)
        {
            var btn = new Button
            {
                Content = "Save",
                Height = 30,
                FontWeight = FontWeights.Bold,
                Margin = new Thickness(0, 10, 0, 0)
            };
            btn.Click += (_, _) => onClick();
            return btn;
        }
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

            stack.Children.Add(ConfigUi.SaveButton(() =>
            {
                node.Configuration ??= new NodeConfiguration();
                node.Configuration.PacketId = (PacketId)combo.SelectedItem;
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
}