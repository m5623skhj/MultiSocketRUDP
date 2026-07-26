using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.Contents.Client;
using System.CodeDom;
using System.Collections.Concurrent;

namespace MultiSocketRUDPBotTester.Bot
{
    public class RuntimeContext
    {
        public Client Client { get; }

        private readonly AsyncLocal<NetBuffer?> currentPacket = new();

        private readonly ConcurrentDictionary<string, object> vars = new();

        private Task pendingAsyncTask = Task.CompletedTask;

        public RuntimeContext(Client client, NetBuffer? packet)
        {
            Client = client;
            currentPacket.Value = packet;
        }

        public NetBuffer? GetPacket()
        {
            return currentPacket.Value;
        }

        public void SetPacket(NetBuffer? newPacket)
        {
            currentPacket.Value = newPacket;
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

        public T GetOrcreate<T>(string key, Func<T> factory) where T : notnull
        {
            return (T)vars.GetOrAdd(key, _ => factory());
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
            ArgumentNullException.ThrowIfNull(task);
            Interlocked.Exchange(ref pendingAsyncTask, task);
        }

        public Task GetAndClearPendingAsyncTask()
        {
            return Interlocked.Exchange(ref pendingAsyncTask, Task.CompletedTask);
        }

        public void Clear()
        {
            vars.Clear();
            currentPacket.Value = null;
            Interlocked.Exchange(ref pendingAsyncTask, Task.CompletedTask);
        }
    }
}
