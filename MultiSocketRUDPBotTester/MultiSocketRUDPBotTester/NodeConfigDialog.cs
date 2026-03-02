using MultiSocketRUDPBotTester.Bot;
using MultiSocketRUDPBotTester.UI;
using System.Windows;
using System.Windows.Controls;

namespace MultiSocketRUDPBotTester
{
    public partial class BotActionGraphWindow : Window
    {
        private NodeConfigPanelRegistry? configRegistry;
        private NodeConfigPanelRegistry ConfigRegistry =>
            configRegistry ??= new NodeConfigPanelRegistry(CreateDynamicPorts);

        private void ShowNodeConfigurationDialog(NodeVisual node)
        {
            var dialog = new Window
            {
                Title = $"Configure {node.NodeType?.Name}",
                Width = 420,
                Height = 400,
                WindowStartupLocation = WindowStartupLocation.CenterOwner,
                Owner = this,
                ResizeMode = ResizeMode.NoResize
            };

            var scroll = new ScrollViewer { VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
            var stack = new StackPanel { Margin = new Thickness(14) };
            scroll.Content = stack;
            dialog.Content = scroll;

            var panel = ConfigRegistry.Find(node);

            if (panel != null)
            {
                panel.Build(stack, node, Log, dialog.Close);
            }
            else
            {
                stack.Children.Add(new TextBlock
                {
                    Text = $"No configuration available for {node.NodeType?.Name}.",
                    TextWrapping = TextWrapping.Wrap
                });
            }

            dialog.ShowDialog();
        }
    }
}