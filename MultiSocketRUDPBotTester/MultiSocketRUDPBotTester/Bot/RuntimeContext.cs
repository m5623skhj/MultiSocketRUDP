using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.Contents.Client;
using System.Collections.Concurrent;

namespace MultiSocketRUDPBotTester.Bot
{
    public class RuntimeContext(Client client, NetBuffer? packet)
    {
        public Client Client { get; } = client;

        private NetBuffer? currentPacket = packet;
        private readonly Lock packetLock = new();

        private readonly ConcurrentDictionary<string, object> vars = new();

        private volatile Task _pendingAsyncTask = Task.CompletedTask;

        public NetBuffer? GetPacket()
        {
            lock (packetLock)
            {
                return currentPacket;
            }
        }

        public void SetPacket(NetBuffer? newPacket)
        {
            lock (packetLock)
            {
                currentPacket = newPacket;
            }
        }

        public void Set<T>(string key, T value) where T : notnull
        {
            vars[key] = value;
        }

        public bool Has(string key) => vars.ContainsKey(key);

        public T Get<T>(string key) where T : notnull
        {
            if (vars.TryGetValue(key, out var v) && v is T t)
            {
                return t;
            }

            throw new KeyNotFoundException($"RuntimeContext missing key: {key}");
        }

        public T GetOrDefault<T>(string key, T defaultValue)
        {
            if (vars.TryGetValue(key, out var v) && v is T t)
            {
                return t;
            }

            return defaultValue;
        }

        public int AtomicIncrement(string key, int delta = 1)
        {
            var result = vars.AddOrUpdate(key, delta,
                (_, existing) => (existing is int i) ? i + delta : delta);
            
            return (int)result;
        }

        public bool Remove(string key)
        {
            return vars.TryRemove(key, out _);
        }

        public void SetPendingAsyncTask(Task task)
        {
            _pendingAsyncTask = task;
        }

        public Task GetAndClearPendingAsyncTask()
        {
            var task = _pendingAsyncTask;
            _pendingAsyncTask = Task.CompletedTask;
            return task;
        }

        public void Clear()
        {
            vars.Clear();
            lock (packetLock)
            {
                currentPacket = null;
            }
            _pendingAsyncTask = Task.CompletedTask;
        }
    }
}
