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
            Title = $"Multi-Socket RUDP Bot Tester - Active Bots: {activeBotCount}";
        }
    }
}
