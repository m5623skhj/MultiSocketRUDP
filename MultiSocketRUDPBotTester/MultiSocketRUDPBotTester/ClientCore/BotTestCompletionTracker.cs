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

        /// <summary>
        /// Marks a bot as disconnected and attempts to complete the test.
        /// This method is thread-safe.
        public void MarkDisconnected()
        {
            Interlocked.Increment(ref disconnectedBotCount);
            TryComplete();
        }

        /// <summary>
        /// Marks the setup phase as complete and attempts to complete the test.
        /// Ensures memory visibility for concurrent access.
        public void MarkSetupComplete()
        {
            Volatile.Write(ref isSetupComplete, 1);
            TryComplete();
        }

        /// <summary>
        /// Transitions the tracker to a cancelled state, preventing further completion notifications.
        /// This operation is atomic.
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
