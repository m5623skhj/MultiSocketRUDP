using MultiSocketRUDPBotTester.ClientCore;
using System.Collections.ObjectModel;
using System.Windows;
using System.Windows.Threading;

namespace MultiSocketRUDPBotTester
{
    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    public partial class MainWindow : Window
    {
        public ObservableCollection<string> Logs { get; set; }

        public MainWindow()
        {
            InitializeComponent();

            Logs = [];
            DataContext = this;

            var timer = new DispatcherTimer
            {
                Interval = TimeSpan.FromSeconds(1)
            };
            timer.Tick += (_, e) => UpdateUi();
            timer.Start();
        }
        private void SetBotActionGraph_Click(object sender, RoutedEventArgs e)
        {
            var window = new BotActionGraphWindow
            {
                Owner = this
            };
            window.Show();
        }

        private async void StartBotTest_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                if (!ushort.TryParse(BotCountTextBox.Text, out var numOfBot) || numOfBot == 0)
                {
                    MessageBox.Show("Please enter a valid bot count (1-65535)", "Invalid Input",
                        MessageBoxButton.OK, MessageBoxImage.Warning);
                    return;
                }

                if (!ushort.TryParse(HostPortTextBox.Text, out var hostPort) || hostPort == 0)
                {
                    MessageBox.Show("Please enter a valid port (1-65535)", "Invalid Input",
                        MessageBoxButton.OK, MessageBoxImage.Warning);
                    return;
                }

                var hostIp = HostIpTextBox.Text.Trim();
                if (string.IsNullOrEmpty(hostIp))
                {
                    MessageBox.Show("Please enter a valid IP address", "Invalid Input",
                        MessageBoxButton.OK, MessageBoxImage.Warning);
                    return;
                }

                HostIpTextBox.IsEnabled = false;
                HostPortTextBox.IsEnabled = false;
                BotCountTextBox.IsEnabled = false;

                BotTesterCore.Instance.SetConnectionInfo(hostIp, hostPort);
                await BotTesterCore.Instance.StartBotTest(numOfBot);

                MessageBox.Show($"Bot test started with {numOfBot} bots", "Success",
                    MessageBoxButton.OK, MessageBoxImage.Information);
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Error: {ex.Message}", "Error",
                    MessageBoxButton.OK, MessageBoxImage.Error);
                BotTesterCore.Instance.StopBotTest();

                HostIpTextBox.IsEnabled = true;
                HostPortTextBox.IsEnabled = true;
                BotCountTextBox.IsEnabled = true;
            }
        }

        private async void StartRttTest_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                if (!int.TryParse(RttSampleCountTextBox.Text, out var sampleCount) || sampleCount <= 0)
                {
                    MessageBox.Show("Please enter a valid RTT sample count.", "Invalid Input",
                        MessageBoxButton.OK, MessageBoxImage.Warning);
                    return;
                }

                if (!int.TryParse(RttTimeoutTextBox.Text, out var timeoutMs) || timeoutMs <= 0)
                {
                    MessageBox.Show("Please enter a valid RTT timeout in milliseconds.", "Invalid Input",
                        MessageBoxButton.OK, MessageBoxImage.Warning);
                    return;
                }

                if (!ushort.TryParse(HostPortTextBox.Text, out var hostPort) || hostPort == 0)
                {
                    MessageBox.Show("Please enter a valid port (1-65535)", "Invalid Input",
                        MessageBoxButton.OK, MessageBoxImage.Warning);
                    return;
                }

                var hostIp = HostIpTextBox.Text.Trim();
                if (string.IsNullOrEmpty(hostIp))
                {
                    MessageBox.Show("Please enter a valid IP address", "Invalid Input",
                        MessageBoxButton.OK, MessageBoxImage.Warning);
                    return;
                }

                HostIpTextBox.IsEnabled = false;
                HostPortTextBox.IsEnabled = false;
                BotCountTextBox.IsEnabled = false;
                RttSampleCountTextBox.IsEnabled = false;
                RttTimeoutTextBox.IsEnabled = false;

                BotTesterCore.Instance.SetConnectionInfo(hostIp, hostPort);
                var summary = await BotTesterCore.Instance.StartRttTest(sampleCount, timeoutMs);

                MessageBox.Show(
                    $"RTT test completed.\nSamples: {summary.SampleCount}\nAvg: {summary.AverageRttMs:F3} ms\nMin: {summary.MinRttMs:F3} ms\np50: {summary.P50RttMs:F3} ms\np95: {summary.P95RttMs:F3} ms\np99: {summary.P99RttMs:F3} ms\nMax: {summary.MaxRttMs:F3} ms\nElapsed: {summary.ElapsedSeconds:F3} s",
                    "RTT Test Complete",
                    MessageBoxButton.OK,
                    MessageBoxImage.Information);
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Error: {ex.Message}", "Error",
                    MessageBoxButton.OK, MessageBoxImage.Error);
                BotTesterCore.Instance.StopBotTest();
            }
            finally
            {
                HostIpTextBox.IsEnabled = true;
                HostPortTextBox.IsEnabled = true;
                BotCountTextBox.IsEnabled = true;
                RttSampleCountTextBox.IsEnabled = true;
                RttTimeoutTextBox.IsEnabled = true;
            }
        }

        private void StopBotTest_Click(object sender, RoutedEventArgs e)
        {
            BotTesterCore.Instance.StopBotTest();

            HostIpTextBox.IsEnabled = true;
            HostPortTextBox.IsEnabled = true;
            BotCountTextBox.IsEnabled = true;
        }

        private void UpdateUi()
        {
            var activeBotCount = BotTesterCore.Instance.GetActiveBotCount();

            var threadCount = ThreadPool.ThreadCount;
            var pendingItems = ThreadPool.PendingWorkItemCount;
            var completedItems = ThreadPool.CompletedWorkItemCount;

            Title = $"Active Bots: {activeBotCount} | " +
                    $"Threads: {threadCount} | " +
                    $"Pending: {pendingItems} | " +
                    $"Completed: {completedItems}";
        }
    }
}
