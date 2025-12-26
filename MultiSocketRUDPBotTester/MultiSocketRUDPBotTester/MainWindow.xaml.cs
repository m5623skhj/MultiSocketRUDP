using ClientCore;
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
            timer.Tick += (s, e) => UpdateUi();
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
                var numOfBot = UInt16.Parse(BotCountTextBox.Text);
                HostIpTextBox.IsEnabled = false;
                HostPortTextBox.IsEnabled = false;
                BotCountTextBox.IsEnabled = false;

                await BotTesterCore.Instance.StartBotTest(numOfBot);
            }
            catch (Exception ex)
            {
                MessageBox.Show($"error {ex.Message}");
                BotTesterCore.Instance.StopBotTest();
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