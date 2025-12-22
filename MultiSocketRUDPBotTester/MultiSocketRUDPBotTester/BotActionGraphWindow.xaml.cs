using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using MultiSocketRUDPBotTester.Bot;

namespace MultiSocketRUDPBotTester
{
    /// <summary>
    /// BotActionGraphWindow.xaml에 대한 상호 작용 논리
    /// </summary>
    public partial class BotActionGraphWindow : Window
    {
        public BotActionGraphWindow()
        {
            InitializeComponent();
            LoadActionNodeTypes();
        }

        private void LoadActionNodeTypes()
        {
            var baseType = typeof(ActionNodeBase);
            var assembly = baseType.Assembly;

            var types = assembly.GetTypes()
                .Where(t => t is { IsClass: true, IsAbstract: false }
                            && baseType.IsAssignableFrom(t))
                .ToList();

            ActionNodeListBox.ItemsSource = types;
        }

        private Border CreateNodeVisual(string title)
        {
            var border = new Border
            {
                Width = 140,
                Height = 60,
                Background = Brushes.DimGray,
                BorderBrush = Brushes.White,
                BorderThickness = new Thickness(1),
                CornerRadius = new CornerRadius(4),
                Child = new TextBlock
                {
                    Text = title,
                    Foreground = Brushes.White,
                    HorizontalAlignment = HorizontalAlignment.Center,
                    VerticalAlignment = VerticalAlignment.Center
                }
            };

            EnableDrag(border);
            return border;
        }

        private void AddNode_Click(object sender, RoutedEventArgs e)
        {
            if (ActionNodeListBox.SelectedItem is not Type type)
            {
                return;
            }

            var node = CreateNodeVisual(type.Name);

            Canvas.SetLeft(node, 100);
            Canvas.SetTop(node, 100);

            GraphCanvas.Children.Add(node);
        }

        private Point dragStart;
        private bool isDragging;

        private void EnableDrag(UIElement element)
        {
            element.MouseLeftButtonDown += (_, e) =>
            {
                isDragging = true;
                dragStart = e.GetPosition(GraphCanvas);
                element.CaptureMouse();
            };

            element.MouseMove += (_, e) =>
            {
                if (!isDragging)
                {
                    return;
                }

                var pos = e.GetPosition(GraphCanvas);

                var left = Canvas.GetLeft(element) + (pos.X - dragStart.X);
                var top = Canvas.GetTop(element) + (pos.Y - dragStart.Y);

                Canvas.SetLeft(element, left);
                Canvas.SetTop(element, top);

                dragStart = pos;
            };

            element.MouseLeftButtonUp += (_, _) =>
            {
                isDragging = false;
                element.ReleaseMouseCapture();
            };
        }
    }
}
