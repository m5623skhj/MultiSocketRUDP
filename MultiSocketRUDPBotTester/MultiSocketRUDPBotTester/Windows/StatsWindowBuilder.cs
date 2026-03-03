using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using MultiSocketRUDPBotTester.Bot;

namespace MultiSocketRUDPBotTester.Windows
{
    public static class StatsWindowBuilder
    {
        public static Window Build(Window owner, NodeStatsTracker statsTracker)
        {
            var window = new Window
            {
                Title = "Node Execution Statistics",
                Width = 900,
                Height = 600,
                Owner = owner
            };

            var grid = new Grid();
            grid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
            grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

            var dataGrid = BuildDataGrid();
            dataGrid.ItemsSource = statsTracker.GetAllStats();
            Grid.SetRow(dataGrid, 0);
            grid.Children.Add(dataGrid);

            var panel = BuildButtonPanel(dataGrid, statsTracker);
            Grid.SetRow(panel, 1);
            grid.Children.Add(panel);

            window.Content = grid;
            return window;
        }

        private static DataGrid BuildDataGrid()
        {
            var dg = new DataGrid
            {
                AutoGenerateColumns = false,
                IsReadOnly = true,
                Margin = new Thickness(10)
            };

            dg.Columns.Add(new DataGridTextColumn
            { Header = "Node", Binding = new Binding("NodeName"), Width = 150 });
            dg.Columns.Add(new DataGridTextColumn
            { Header = "Exec", Binding = new Binding("ExecutionCount"), Width = 60 });
            dg.Columns.Add(new DataGridTextColumn
            { Header = "Success%", Binding = new Binding("SuccessRate") { StringFormat = "F1" }, Width = 80 });
            dg.Columns.Add(new DataGridTextColumn
            { Header = "Avg(ms)", Binding = new Binding("AverageExecutionTimeMs") { StringFormat = "F2" }, Width = 80 });
            dg.Columns.Add(new DataGridTextColumn
            { Header = "Min(ms)", Binding = new Binding("MinExecutionTimeMs"), Width = 70 });
            dg.Columns.Add(new DataGridTextColumn
            { Header = "Max(ms)", Binding = new Binding("MaxExecutionTimeMs"), Width = 70 });

            return dg;
        }

        private static StackPanel BuildButtonPanel(DataGrid dataGrid, NodeStatsTracker statsTracker)
        {
            var panel = new StackPanel
            {
                Orientation = Orientation.Horizontal,
                HorizontalAlignment = HorizontalAlignment.Right,
                Margin = new Thickness(10)
            };

            var refreshBtn = new Button { Content = "Refresh", Width = 80 };
            refreshBtn.Click += (_, _) =>
            {
                dataGrid.ItemsSource = null;
                dataGrid.ItemsSource = statsTracker.GetAllStats();
            };

            var resetBtn = new Button
            {
                Content = "Reset",
                Width = 80,
                Margin = new Thickness(5, 0, 0, 0)
            };
            resetBtn.Click += (_, _) =>
            {
                if (MessageBox.Show("Reset all?", "Confirm", MessageBoxButton.YesNo)
                    != MessageBoxResult.Yes) return;

                statsTracker.Reset();
                dataGrid.ItemsSource = null;
            };

            panel.Children.Add(refreshBtn);
            panel.Children.Add(resetBtn);
            return panel;
        }
    }
}