using MultiSocketRUDPBotTester.Contents.Client;

namespace MultiSocketRUDPBotTester.ClientCore
{
    public sealed class BotTestResult
    {
        public int BotCount { get; init; }
        public RttTestSummary RttSummary { get; init; } = new();
    }
}
