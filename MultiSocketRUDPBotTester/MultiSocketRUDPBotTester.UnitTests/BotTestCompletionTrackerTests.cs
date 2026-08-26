using MultiSocketRUDPBotTester.ClientCore;

namespace MultiSocketRUDPBotTester.UnitTests;

public sealed class BotTestCompletionTrackerTests
{
    /// <summary>
    /// 설정 완료 전에 연결이 끊기면 설정 완료까지 종료 알림을 보류하는지 확인합니다.
    /// </summary>
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

    /// <summary>
    /// 마지막 봇의 연결 해제 시 완료 알림을 정확히 한 번 발생시키는지 확인합니다.
    /// </summary>
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

    /// <summary>
    /// 취소된 추적기가 이후 연결 해제에 대해 완료 알림을 발생시키지 않는지 확인합니다.
    /// </summary>
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

    /// <summary>
    /// 동시 연결 해제에서도 완료 알림이 한 번만 발생하는지 확인합니다.
    /// </summary>
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
