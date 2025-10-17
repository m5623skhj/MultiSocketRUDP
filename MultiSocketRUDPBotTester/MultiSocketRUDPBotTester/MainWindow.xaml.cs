using CleintCore;
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
        private MultiSocketRUDPBotCore core;
        public ObservableCollection<string> Logs { get; set; }

        public MainWindow()
        {
            InitializeComponent();

            core = new MultiSocketRUDPBotCore();
            Logs = new ObservableCollection<string>();
            DataContext = this;

            DispatcherTimer timer = new DispatcherTimer();
            timer.Interval = TimeSpan.FromSeconds(1);
            timer.Tick += (s, e) => UpdateUI();
            timer.Start();
        }

        private void UpdateUI()
        {
        }
    }
}