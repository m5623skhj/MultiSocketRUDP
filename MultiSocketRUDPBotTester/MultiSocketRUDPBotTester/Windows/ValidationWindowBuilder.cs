using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using MultiSocketRUDPBotTester.Bot;

namespace MultiSocketRUDPBotTester.Windows
{
    public static class ValidationWindowBuilder
    {
        public static void ShowDialog(Window owner, GraphValidationResult result)
        {
            var window = new Window
            {
                Title = "Validation Results",
                Width = 700,
                Height = 500,
                Owner = owner
            };

            var stack = new StackPanel { Margin = new Thickness(10) };

            stack.Children.Add(BuildSummaryText(result));
            stack.Children.Add(BuildStatsText(result));
            stack.Children.Add(BuildIssueScroll(result));
            stack.Children.Add(BuildCloseButton(window));

            window.Content = stack;
            window.ShowDialog();
        }

        private static TextBlock BuildSummaryText(GraphValidationResult result) => new()
        {
            Text = result.IsValid ? "✓ Valid" : $"✗ {result.ErrorCount} Errors",
            FontSize = 18,
            FontWeight = FontWeights.Bold,
            Foreground = result.IsValid ? Brushes.Green : Brushes.Red,
            Margin = new Thickness(0, 0, 0, 10)
        };

        private static TextBlock BuildStatsText(GraphValidationResult result) => new()
        {
            Text = $"Errors: {result.ErrorCount}, " +
                     $"Warnings: {result.WarningCount}, " +
                     $"Info: {result.InfoCount}",
            Margin = new Thickness(0, 0, 0, 10)
        };

        private static ScrollViewer BuildIssueScroll(GraphValidationResult result)
        {
            var scroll = new ScrollViewer { Height = 350 };
            var issueStack = new StackPanel();

            foreach (var issue in result.Issues)
            {
                issueStack.Children.Add(new TextBlock
                {
                    Text = $"[{issue.Severity}] {issue.NodeName}: {issue.Message}",
                    Foreground = issue.Severity == ValidationSeverity.Error ? Brushes.Red :
                                      issue.Severity == ValidationSeverity.Warning ? Brushes.Orange :
                                                                                     Brushes.Blue,
                    TextWrapping = TextWrapping.Wrap,
                    Margin = new Thickness(0, 2, 0, 2)
                });
            }

            scroll.Content = issueStack;
            return scroll;
        }

        private static Button BuildCloseButton(Window window)
        {
            var btn = new Button
            {
                Content = "Close",
                Width = 80,
                HorizontalAlignment = HorizontalAlignment.Right
            };
            btn.Click += (_, _) => window.Close();
            return btn;
        }
    }
}