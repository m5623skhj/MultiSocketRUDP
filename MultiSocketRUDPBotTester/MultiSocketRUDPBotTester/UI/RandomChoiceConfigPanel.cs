using MultiSocketRUDPBotTester.Bot;
using System.Windows;
using System.Windows.Controls;

namespace MultiSocketRUDPBotTester.UI
{
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
}
