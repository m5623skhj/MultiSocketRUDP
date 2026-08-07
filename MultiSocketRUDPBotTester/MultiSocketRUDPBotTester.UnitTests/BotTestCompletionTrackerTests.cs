using MultiSocketRUDPBotTester.ClientCore;

namespace MultiSocketRUDPBotTester.UnitTests;

public sealed class BotTestCompletionTrackerTests
{
    [Fact]
    public void DisconnectBeforeSetupWaitsUntilSetupCompletes()
    {
        var notificationCount = 0;
        var tracker = new BotTestCompletionTracker(
            1,
            completedBotCount => notificationCount += completedBotCount);

        tracker.MarkDisconnected();
        Assert.Equal(0, notificationCount);

        tracker.MarkSetupComplete();
        Assert.Equal(1, notificationCount);
    }

    [Fact]
    public void LastDisconnectRaisesCompletionExactlyOnce()
    {
        var notificationCount = 0;
        var tracker = new BotTestCompletionTracker(
            2,
            completedBotCount => notificationCount += completedBotCount);
        tracker.MarkSetupComplete();

        tracker.MarkDisconnected();
        tracker.MarkDisconnected();
        tracker.MarkDisconnected();

        Assert.Equal(2, notificationCount);
    }

    [Fact]
    public void CancelSuppressesCompletionNotification()
    {
        var notificationCount = 0;
        var tracker = new BotTestCompletionTracker(
            1,
            completedBotCount => notificationCount += completedBotCount);
        tracker.MarkSetupComplete();

        tracker.Cancel();
        tracker.MarkDisconnected();

        Assert.Equal(0, notificationCount);
    }

    [Fact]
    public void ConcurrentDisconnectsRaiseSingleCompletionNotification()
    {
        const int BotCount = 64;
        var notificationCount = 0;
        var tracker = new BotTestCompletionTracker(
            BotCount,
            _ => Interlocked.Increment(ref notificationCount));
        tracker.MarkSetupComplete();

        Parallel.For(0, BotCount, _ => tracker.MarkDisconnected());

        Assert.Equal(1, notificationCount);
    }
}
