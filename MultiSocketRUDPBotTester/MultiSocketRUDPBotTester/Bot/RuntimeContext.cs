using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.Contents.Client;
using System.Collections.Concurrent;

namespace MultiSocketRUDPBotTester.Bot
{
    public class RuntimeContext(Client client, NetBuffer? packet)
    {
        public Client Client { get; } = client;
        public NetBuffer? Packet { get; } = packet;

        private readonly ConcurrentDictionary<string, object> vars = new();

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
    }
}
