using MultiSocketRUDPBotTester.Bot;
using System.Windows;
using System.Windows.Controls;

namespace MultiSocketRUDPBotTester.UI
{
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
