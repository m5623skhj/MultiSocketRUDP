using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace MultiSocketRUDPBotTester.UI
{
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
}