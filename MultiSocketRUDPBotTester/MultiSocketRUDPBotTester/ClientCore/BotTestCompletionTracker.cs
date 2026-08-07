namespace MultiSocketRUDPBotTester.ClientCore
{
    internal sealed class BotTestCompletionTracker
    {
        private const int ActiveState = 0;
        private const int CompletedState = 1;
        private const int CancelledState = 2;

        private readonly int expectedBotCount;
        private readonly Action<int> onCompleted;

        private int disconnectedBotCount;
        private int isSetupComplete;
        private int state = ActiveState;

        public BotTestCompletionTracker(int inExpectedBotCount, Action<int> inOnCompleted)
        {
            if (inExpectedBotCount <= 0)
            {
                throw new ArgumentOutOfRangeException(nameof(inExpectedBotCount));
            }

            expectedBotCount = inExpectedBotCount;
            onCompleted = inOnCompleted ?? throw new ArgumentNullException(nameof(inOnCompleted));
        }

        public void MarkDisconnected()
        {
            Interlocked.Increment(ref disconnectedBotCount);
            TryComplete();
        }

        public void MarkSetupComplete()
        {
            Volatile.Write(ref isSetupComplete, 1);
            TryComplete();
        }

        public void Cancel()
        {
            Interlocked.CompareExchange(ref state, CancelledState, ActiveState);
        }

        private void TryComplete()
        {
            if (Volatile.Read(ref isSetupComplete) == 0 ||
                Volatile.Read(ref disconnectedBotCount) != expectedBotCount)
            {
                return;
            }

            if (Interlocked.CompareExchange(ref state, CompletedState, ActiveState) == ActiveState)
            {
                onCompleted(expectedBotCount);
            }
        }
    }
}
