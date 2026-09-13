using System.Buffers.Binary;
using System.Diagnostics;
using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.ClientCore;

namespace MultiSocketRUDPBotTester.RttBenchmark;

public sealed record ChannelMetrics(int Attempted, int Accepted, int Received, int Late,
    double DeliveryPercent, double ResponsesPerSecond, double? P50Ms, double? P95Ms, double? P99Ms);
public sealed record ChannelRun(ChannelMetrics Reliable, ChannelMetrics Unreliable, bool Connected, double ElapsedSeconds);
public sealed record ChannelResult(int SchemaVersion, string CommitSha, string ScenarioName,
    DateTimeOffset RecordedAtUtc, int SampleCount, int WarmupSampleCount, int TimeoutMs,
    int ServerThreadCount, string OperatingSystem, int ProcessorCount, IReadOnlyList<ChannelRun> Runs)
{
    public int RunCount => Runs.Count;
    public bool Healthy => Runs.All(run => run.Connected && new[] { run.Reliable, run.Unreliable }
        .All(channel => channel.Attempted == 0 || (channel.Accepted == channel.Attempted && channel.Received > 0)));
}

/// <summary>요청 ID로 응답을 대응시키며 큐 대기를 포함한 RTT를 기록합니다.</summary>
public sealed class ChannelSamples(int timeoutMs)
{
    private readonly object gate = new();
    private readonly Dictionary<ulong, (bool Unreliable, long Timestamp)> pending = [];
    private readonly List<double>[] samples = [[], []];
    private readonly int[] attempted = new int[2], accepted = new int[2], late = new int[2];

    public void Begin(ulong id, bool unreliable, long timestamp)
    {
        lock (gate)
        {
            pending.Add(id, (unreliable, timestamp));
            attempted[unreliable ? 1 : 0]++;
        }
    }
    public void Admission(ulong id, bool unreliable, bool success)
    {
        lock (gate)
        {
            if (success) accepted[unreliable ? 1 : 0]++;
            else pending.Remove(id);
        }
    }
    public void Receive(ulong id, bool unreliable, long timestamp)
    {
        lock (gate)
        {
            if (!pending.TryGetValue(id, out var request) || request.Unreliable != unreliable) return;
            pending.Remove(id);
            var elapsed = Stopwatch.GetElapsedTime(request.Timestamp, timestamp).TotalMilliseconds;
            var index = unreliable ? 1 : 0;
            if (elapsed > timeoutMs) late[index]++;
            else samples[index].Add(elapsed);
        }
    }
    public ChannelMetrics Snapshot(bool unreliable, double elapsedSeconds)
    {
        lock (gate)
        {
            var index = unreliable ? 1 : 0;
            var sorted = samples[index].Order().ToArray();
            double? Percentile(double p) => sorted.Length == 0 ? null : sorted[(int)Math.Ceiling(sorted.Length * p) - 1];
            return new(attempted[index], accepted[index], sorted.Length, late[index],
                attempted[index] == 0 ? 0 : 100.0 * sorted.Length / attempted[index],
                sorted.Length / elapsedSeconds, Percentile(.5), Percentile(.95), Percentile(.99));
        }
    }
}

public static class ChannelRttBenchmark
{
    public static async Task<ChannelResult> RunAsync(RttBenchmarkOptions options)
    {
        if (options.ScenarioName is not ("unreliable-only" or "reliable-baseline" or "mixed"))
            throw new ArgumentException("Channel scenario must be unreliable-only, reliable-baseline or mixed.");
        if (options.LossRate != 0) throw new ArgumentException("Per-commit channel runs use zero simulated loss.");
        var runs = new List<ChannelRun>();
        for (var run = 0; run < options.RunCount; run++)
        {
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(options.RunTimeoutSeconds));
            using var getter = new SessionGetter();
            await getter.ConnectAsync(options.Host, options.Port).WaitAsync(timeout.Token);
            var bytes = new byte[1024];
            var length = 0;
            while (length < 5 || length < 5 + BinaryPrimitives.ReadUInt16LittleEndian(bytes.AsSpan(1)))
            {
                var count = await getter.ReceiveAsync(bytes, length).WaitAsync(timeout.Token);
                if (count == 0) throw new IOException("Incomplete broker response.");
                length += count;
                if (length == bytes.Length) throw new IOException("Broker response is too large.");
            }
            var client = new EchoClient(bytes[5..length]);
            try
            {
                while (!client.IsConnected()) await Task.Delay(1, timeout.Token);
                client.ConfigureRetransmission(100, 50);
                if (options.WarmupSampleCount > 0)
                    await Phase(client, options, options.WarmupSampleCount, timeout.Token);
                runs.Add(await Phase(client, options, options.SampleCount, timeout.Token));
            }
            finally { client.Disconnect(); }
        }
        return new(1, options.CommitSha, options.ScenarioName, DateTimeOffset.UtcNow,
            options.SampleCount, options.WarmupSampleCount, options.TimeoutMs, options.ServerThreadCount,
            System.Runtime.InteropServices.RuntimeInformation.OSDescription, Environment.ProcessorCount, runs);
    }

    private static async Task<ChannelRun> Phase(EchoClient client, RttBenchmarkOptions options, int count, CancellationToken token)
    {
        var samples = new ChannelSamples(options.TimeoutMs);
        client.Samples = samples;
        var started = Stopwatch.GetTimestamp();
        // Identical 10 ms batches: up to ten unreliable requests and one reliable request.
        for (var offset = 0; offset < count; offset += 10)
        {
            token.ThrowIfCancellationRequested();
            if (!client.IsConnected()) break;
            if (options.ScenarioName != "reliable-baseline")
                for (var i = offset; i < Math.Min(count, offset + 10); i++) await client.SendEcho(true);
            if (options.ScenarioName != "unreliable-only") await client.SendEcho(false);
            await Task.Delay(10, token);
        }
        // A fixed drain interval prevents fast responses from hiding missing or queued packets.
        await Task.Delay(options.TimeoutMs, token);
        var elapsed = Stopwatch.GetElapsedTime(started).TotalSeconds;
        return new(samples.Snapshot(false, elapsed), samples.Snapshot(true, elapsed), client.IsConnected(), elapsed);
    }

    private sealed class EchoClient(byte[] response) : RudpSession(response)
    {
        private const PacketId RequestPacketId = (PacketId)7;
        private const PacketId ResponsePacketId = (PacketId)8;
        public volatile ChannelSamples? Samples;
        private ulong nextRequestId;
        public async Task SendEcho(bool unreliable)
        {
            var id = ++nextRequestId;
            var buffer = new NetBuffer(64);
            buffer.ReserveHeader();
            buffer.WriteULong(id);
            buffer.WriteByte(unreliable ? (byte)1 : (byte)0);
            var samples = Samples!;
            samples.Begin(id, unreliable, Stopwatch.GetTimestamp());
            var accepted = false;
            try
            {
                if (unreliable) accepted = SendUnreliablePacket(buffer, RequestPacketId);
                else { await SendPacket(buffer, RequestPacketId); accepted = true; }
            }
            catch (Exception) when (!IsConnected()) { }
            finally { samples.Admission(id, unreliable, accepted); }
        }
        protected override bool TryHandleRecvFastPath(PacketId packetId, NetBuffer buffer)
        {
            if (packetId != ResponsePacketId) return false;
            var id = buffer.ReadULong();
            var channel = buffer.ReadByte();
            if (channel <= 1) Samples?.Receive(id, channel == 1, Stopwatch.GetTimestamp());
            return true;
        }
        protected override void OnRecvPacket(PacketId packetId, NetBuffer buffer) { }
    }
}
