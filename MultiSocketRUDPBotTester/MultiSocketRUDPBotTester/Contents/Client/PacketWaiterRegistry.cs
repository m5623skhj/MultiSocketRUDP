using System.Collections.Concurrent;
using MultiSocketRUDPBotTester.Buffer;

namespace MultiSocketRUDPBotTester.Contents.Client
{
    internal sealed class PacketWaiterRegistry
    {
        private readonly ConcurrentDictionary<WaiterKey, TaskCompletionSource<NetBuffer?>> waiters = new();
        private long nextWaiterId;

        public async Task<NetBuffer?> WaitAsync(
            PacketId packetId,
            int timeoutMs,
            CancellationToken cancellationToken)
        {
            if (timeoutMs <= 0)
            {
                throw new ArgumentOutOfRangeException(nameof(timeoutMs));
            }

            var source = new TaskCompletionSource<NetBuffer?>(
                TaskCreationOptions.RunContinuationsAsynchronously);
            var key = new WaiterKey(packetId, Interlocked.Increment(ref nextWaiterId));
            waiters[key] = source;

            try
            {
                return await source.Task
                    .WaitAsync(TimeSpan.FromMilliseconds(timeoutMs), cancellationToken)
                    .ConfigureAwait(false);
            }
            catch (TimeoutException)
            {
                return null;
            }
            finally
            {
                waiters.TryRemove(key, out _);
            }
        }

        public void Complete(PacketId packetId, NetBuffer buffer)
        {
            var matchingKeys = waiters.Keys
                .Where(key => key.PacketId == packetId)
                .ToArray();

            foreach (var key in matchingKeys)
            {
                if (waiters.TryRemove(key, out var source))
                {
                    source.TrySetResult(buffer);
                }
            }
        }

        internal int GetPendingCount() => waiters.Count;

        private readonly record struct WaiterKey(PacketId PacketId, long Id);
    }
}
