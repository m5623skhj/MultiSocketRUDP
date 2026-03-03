using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace MultiSocketRUDPBotTester.AI
{
    public static class AiTreeWindowBuilder
    {
        private const string GenerateButtonTag = "GenerateBtn";

        public static Window Build(
            Window owner,
            out TextBox inputBox,
            out TextBox outputBox,
            out Button applyBtn)
        {
            var window = new Window
            {
                Title = "AI Tree Generator",
                Width = 700,
                Height = 600,
                Owner = owner,
                WindowStartupLocation = WindowStartupLocation.CenterOwner
            };

            var grid = new Grid { Margin = new Thickness(10) };
            grid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
            grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            grid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(200) });
            grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

            var capturedInputBox = new TextBox
            {
                Height = 100,
                AcceptsReturn = true,
                TextWrapping = TextWrapping.Wrap,
                VerticalScrollBarVisibility = ScrollBarVisibility.Auto
            };

            var inputSection = new StackPanel();
            inputSection.Children.Add(new TextBlock
            {
                Text = "Describe the test scenario:",
                FontWeight = FontWeights.Bold,
                Margin = new Thickness(0, 0, 0, 5)
            });
            inputSection.Children.Add(new TextBlock
            {
                Text = "Example: \"If player has money, go to shop and buy item 1\"",
                FontSize = 11,
                Foreground = Brushes.Gray,
                Margin = new Thickness(0, 0, 0, 10)
            });
            inputSection.Children.Add(capturedInputBox);
            Grid.SetRow(inputSection, 0);
            grid.Children.Add(inputSection);

            var generateBtn = new Button
            {
                Content = "Generate Tree",
                Height = 40,
                Margin = new Thickness(0, 10, 0, 10),
                Background = new SolidColorBrush(Color.FromRgb(33, 150, 243)),
                Foreground = Brushes.White,
                FontWeight = FontWeights.Bold,
                FontSize = 14,
                Tag = GenerateButtonTag
            };
            Grid.SetRow(generateBtn, 1);
            grid.Children.Add(generateBtn);

            var capturedOutputBox = new TextBox
            {
                Height = 180,
                IsReadOnly = true,
                TextWrapping = TextWrapping.Wrap,
                VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
                Background = new SolidColorBrush(Color.FromRgb(250, 250, 250))
            };

            var outputSection = new StackPanel { Margin = new Thickness(0, 10, 0, 0) };
            outputSection.Children.Add(new TextBlock
            {
                Text = "AI Response:",
                FontWeight = FontWeights.Bold,
                Margin = new Thickness(0, 0, 0, 5)
            });
            outputSection.Children.Add(capturedOutputBox);
            Grid.SetRow(outputSection, 2);
            grid.Children.Add(outputSection);

            var capturedApplyBtn = new Button
            {
                Content = "Apply to Canvas",
                Width = 120,
                Height = 35,
                Background = new SolidColorBrush(Color.FromRgb(76, 175, 80)),
                Foreground = Brushes.White,
                FontWeight = FontWeights.Bold,
                IsEnabled = false
            };

            var closeBtn = new Button
            {
                Content = "Close",
                Width = 80,
                Height = 35,
                Margin = new Thickness(10, 0, 0, 0)
            };
            closeBtn.Click += (_, _) => window.Close();

            var btnPanel = new StackPanel
            {
                Orientation = Orientation.Horizontal,
                HorizontalAlignment = HorizontalAlignment.Right,
                Margin = new Thickness(0, 10, 0, 0)
            };
            btnPanel.Children.Add(capturedApplyBtn);
            btnPanel.Children.Add(closeBtn);
            Grid.SetRow(btnPanel, 3);
            grid.Children.Add(btnPanel);

            window.Content = grid;

            inputBox = capturedInputBox;
            outputBox = capturedOutputBox;
            applyBtn = capturedApplyBtn;

            return window;
        }

        public static Button GetGenerateButton(Window window)
        {
            var grid = (Grid)window.Content;
            return grid.Children.OfType<Button>()
                       .First(b => b.Tag as string == GenerateButtonTag);
        }
    }
}