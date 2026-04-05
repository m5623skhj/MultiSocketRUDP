using System.Diagnostics;
using MultiSocketRUDPBotTester.Buffer;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using Serilog;
using System.Threading.Channels;

namespace MultiSocketRUDPBotTester.ClientCore
{
    public enum SessionState
    {
        Disconnected,
        Connecting,
        Connected,
        Disconnecting
    }

    public enum ConnectResultCode : byte
    {
        Success = 0,
        ServerFull = 1,
        AlreadyConnectedSession = 2,
        CreateSocketFailed = 3,
        RioInitFailed = 4,
        DoRecvFailed = 5
    }

    public class SessionInfo
    {
        public static readonly int SessionKeySize = 16;
        public static readonly int SessionSaltSize = 16;

        public SessionIdType SessionId { get; set; }
        public byte[] SessionKey { get; set; } = new byte[16];
        public byte[] SessionSalt { get; set; } = new byte[16];

        public SessionState SessionState { get; set; }
        public AesGcm? AesGcm { get; set; }
    }

    public class TargetServerInfo
    {
        public string ServerIp { get; set; } = "";
        public ushort ServerPort { get; set; }
    }

    public class HeldPacket
    {
        public PacketId PacketId { get; set; }
        public NetBuffer Buffer { get; set; } = null!;
    }

    public class HoldingPacketStore
    {
        private readonly SortedDictionary<PacketSequence, HeldPacket> holdingPackets = [];
        private readonly Lock holdingPacketsLock = new();

        public void Add(PacketSequence sequence, HeldPacket packet)
        {
            lock (holdingPacketsLock)
            {
                holdingPackets.TryAdd(sequence, packet);
            }
        }

        public void Remove(PacketSequence sequence)
        {
            lock (holdingPacketsLock)
            {
                holdingPackets.Remove(sequence);
            }
        }

        public bool TryGetFirst(out PacketSequence sequence, out HeldPacket packet)
        {
            lock (holdingPacketsLock)
            {
                if (holdingPackets.Count == 0)
                {
                    sequence = default;
                    packet = null!;
                    return false;
                }

                var first = holdingPackets.First();
                sequence = first.Key;
                packet = first.Value;
                return true;
            }
        }

        public void Clear()
        {
            lock (holdingPacketsLock)
            {
                holdingPackets.Clear();
            }
        }
    }

    public abstract class RudpSession
    {
        private const ulong LoginPacketSequence = 0;
        private const int HeaderSize = 5;
        private const int RetransmissionWakeUpMs = 30;

        public SessionInfo SessionInfo { get; } = new() { SessionState = SessionState.Disconnected };
        public TargetServerInfo TargetServerInfo { get; } = new();

        private UdpClient udpClient = null!;
        private PacketSequence lastSendSequence = 0;

        private readonly Lock expectedRecvSequenceLock = new();
        private PacketSequence expectedRecvSequence;

        private readonly Lock aesGcmLock = new();

        private readonly HoldingPacketStore holdingPacketStore = new();
        private readonly BufferStore bufferStore = new();

        public CancellationTokenSource CancellationToken = new();

        private volatile bool isConnected;
        private int isDisposed;

        public Action<SessionIdType>? OnSessionDisconnected { get; set; }

        private readonly Channel<Action> recvProcessingChannel =
            Channel.CreateUnbounded<Action>(
                new UnboundedChannelOptions
                {
                    SingleReader = true,
                    SingleWriter = true
                });

        protected abstract void OnRecvPacket(PacketId packetId, NetBuffer buffer);
        protected virtual void OnConnected() { }
        protected virtual void OnDisconnected() { }

        protected RudpSession(byte[] sessionInfoStream)
        {
            ParseSessionBrokerResponse(sessionInfoStream);
        }

        public SessionIdType GetSessionId() => SessionInfo.SessionId;
        public bool IsConnected() => isConnected;

        private void ParseSessionBrokerResponse(byte[] data)
        {
            var buffer = new NetBuffer(data.Length);
            buffer.WriteBytes(data);

            var resultCode = (ConnectResultCode)buffer.ReadByte();
            if (resultCode != ConnectResultCode.Success)
                throw new Exception($"Session broker response error: {resultCode}");

            TargetServerInfo.ServerIp = buffer.ReadString();
            TargetServerInfo.ServerPort = buffer.ReadUShort();
            SessionInfo.SessionId = buffer.ReadUShort();
            SessionInfo.SessionKey = buffer.ReadBytes(SessionInfo.SessionKeySize);
            SessionInfo.SessionSalt = buffer.ReadBytes(SessionInfo.SessionSaltSize);

            SessionInfo.AesGcm = new AesGcm(SessionInfo.SessionKey, 16);
            SessionInfo.SessionState = SessionState.Connecting;

            udpClient = new UdpClient();
            udpClient.Connect(new IPEndPoint(
                IPAddress.Parse(TargetServerInfo.ServerIp), TargetServerInfo.ServerPort));

            _ = Task.Run(ReceiveAsync);
            _ = Task.Run(PacketProcessorAsync);
            _ = Task.Run(RetransmissionAsync);
            _ = Task.Run(SendConnectPacketAsync);
        }

        public void Disconnect() => Cleanup();

        public async Task SendPacket(
            NetBuffer packetBuffer,
            PacketId packetId,
            PacketType packetType = PacketType.SendType)
        {
            var sequence = Interlocked.Increment(ref lastSendSequence);

            packetBuffer.InsertPacketType(packetType);
            packetBuffer.InsertPacketSequence(sequence);
            packetBuffer.InsertPacketId(packetId);

            lock (aesGcmLock)
            {
                Debug.Assert(SessionInfo.AesGcm != null);
                NetBuffer.EncodePacket(
                    SessionInfo.AesGcm, packetBuffer, sequence,
                    PacketDirection.ClientToServer, SessionInfo.SessionSalt, isCorePacket: false);
            }

            await SendPacketInternal(new SendPacketInfo(packetBuffer, sequence)).ConfigureAwait(false);
        }

        private async Task<bool> SendPacketInternal(SendPacketInfo sendPacketInfo)
        {
            sendPacketInfo.InitializeSendTimestamp(CommonFunc.GetNowMs());
            bufferStore.EnqueueSendBuffer(sendPacketInfo);

            await udpClient.SendAsync(sendPacketInfo.SentBuffer.GetPacketMemory()).ConfigureAwait(false);

            return true;
        }

        private async Task SendConnectPacketAsync()
        {
            try
            {
                var buffer = MakeConnectPacket();

                lock (aesGcmLock)
                {
                    Debug.Assert(SessionInfo.AesGcm != null);
                    NetBuffer.EncodePacket(
                        SessionInfo.AesGcm, buffer, LoginPacketSequence,
                        PacketDirection.ClientToServer, SessionInfo.SessionSalt, isCorePacket: true);
                }

                if (!await SendPacketInternal(new SendPacketInfo(buffer, LoginPacketSequence))
                        .ConfigureAwait(false))
                {
                    Log.Error("SendConnectPacketAsync() failed. SessionId {Id}", SessionInfo.SessionId);
                }
            }
            catch (Exception ex)
            {
                Log.Error("SendConnectPacketAsync() exception: {Error}", ex.Message);
            }
        }

        private NetBuffer MakeConnectPacket()
        {
            var buffer = new NetBuffer(64);
            buffer.BuildConnectPacket(SessionInfo.SessionId);
            return buffer;
        }

        private async Task ReceiveAsync()
        {
            try
            {
                while (!CancellationToken.Token.IsCancellationRequested)
                {
                    var result = await udpClient
                        .ReceiveAsync(CancellationToken.Token)
                        .ConfigureAwait(false);

                    try
                    {
                        await ProcessReceivedPacketAsync(result.Buffer).ConfigureAwait(false);
                    }
                    catch (Exception ex)
                    {
                        Log.Error("ProcessReceivedPacket error (loop continues): {Error}", ex.Message);
                    }
                }
            }
            catch (OperationCanceledException) { Log.Information("ReceiveAsync: Cancelled"); }
            catch (ObjectDisposedException) { }
            catch (Exception ex)
            {
                if (!CancellationToken.Token.IsCancellationRequested)
                    Log.Error("ReceiveAsync fatal error: {Error}", ex.ToString());
            }
        }

        private async Task PacketProcessorAsync()
        {
            try
            {
                await foreach (var action in
                    recvProcessingChannel.Reader
                        .ReadAllAsync(CancellationToken.Token)
                        .ConfigureAwait(false))
                {
                    try
                    {
                        action();
                    }
                    catch (Exception ex)
                    {
                        Log.Error("PacketProcessorAsync: action failed: {Error}", ex.Message);
                    }
                }
            }
            catch (OperationCanceledException)
            {
                Log.Information("PacketProcessorAsync: Cancelled");
            }
        }

        private async Task ProcessReceivedPacketAsync(byte[] data)
        {
            if (SessionInfo.AesGcm == null)
            {
                Log.Error("AesGcm is null, cannot decode packet");
                return;
            }

            var buffer = new NetBuffer(data.Length);
            buffer.WriteBytes(data);

            buffer.SkipBytes(HeaderSize);
            var packetType = (PacketType)buffer.ReadByte();
            var isCorePacket = packetType != PacketType.SendType;

            PacketDirection direction;
            switch (packetType)
            {
                case PacketType.HeartbeatType:
                case PacketType.SendType:
                    direction = PacketDirection.ServerToClient;
                    break;
                case PacketType.SendReplyType:
                    direction = PacketDirection.ServerToClientReply;
                    break;
                default:
                    Log.Warning("ProcessReceivedPacketAsync: Unknown packet type {Type}", packetType);
                    return;
            }

            bool decoded;
            lock (aesGcmLock)
            {
                decoded = NetBuffer.DecodePacket(
                    SessionInfo.AesGcm, buffer, isCorePacket, SessionInfo.SessionSalt, direction);
            }

            if (!decoded)
            {
                Log.Error("DecodePacket failed (type={Type})", packetType);
                return;
            }

            var packetSequence = buffer.ReadULong();
            var packetId = PacketId.InvalidPacketId;
            if (!isCorePacket)
                packetId = (PacketId)buffer.ReadUInt();

            switch (packetType)
            {
                case PacketType.HeartbeatType:
                    await SendReplyToServerAsync(packetSequence).ConfigureAwait(false);
                    break;

                case PacketType.SendType:
                    await SendReplyToServerAsync(packetSequence).ConfigureAwait(false);

                    var packetsToProcess = CollectPacketsToProcess(packetSequence, packetId, buffer);
                    foreach (var (pid, buf) in packetsToProcess)
                    {
                        var capturedPid = pid;
                        var capturedBuf = buf;
                        recvProcessingChannel.Writer.TryWrite(() => OnRecvPacket(capturedPid, capturedBuf));
                    }
                    break;

                case PacketType.SendReplyType:
                    OnSendReply(packetSequence);
                    break;
            }
        }

        private List<(PacketId packetId, NetBuffer buffer)> CollectPacketsToProcess(
            PacketSequence packetSequence,
            PacketId packetId,
            NetBuffer buffer)
        {
            var result = new List<(PacketId, NetBuffer)>();

            lock (expectedRecvSequenceLock)
            {
                if (packetSequence <= expectedRecvSequence)
                    return result;

                if (packetSequence == expectedRecvSequence + 1)
                {
                    expectedRecvSequence = packetSequence;
                    result.Add((packetId, buffer));

                    while (holdingPacketStore.TryGetFirst(out var nextSeq, out var held))
                    {
                        if (nextSeq != expectedRecvSequence + 1)
                            break;

                        expectedRecvSequence = nextSeq;
                        holdingPacketStore.Remove(nextSeq);
                        result.Add((held.PacketId, held.Buffer));
                    }
                }
                else
                {
                    holdingPacketStore.Add(packetSequence,
                        new HeldPacket { PacketId = packetId, Buffer = buffer });
                }
            }

            return result;
        }

        private async Task SendReplyToServerAsync(PacketSequence packetSequence)
        {
            try
            {
                var replyBuffer = new NetBuffer(64);
                replyBuffer.BuildCorePacket(PacketType.SendReplyType, packetSequence);

                lock (aesGcmLock)
                {
                    if (SessionInfo.AesGcm == null)
                    {
                        Log.Warning("SendReplyToServerAsync: AesGcm is null, skipping seq {Seq}", packetSequence);
                        return;
                    }

                    NetBuffer.EncodePacket(
                        SessionInfo.AesGcm, replyBuffer, packetSequence,
                        PacketDirection.ClientToServerReply, SessionInfo.SessionSalt, isCorePacket: true);
                }

                var client = udpClient;
                if (client == null)
                {
                    Log.Warning("SendReplyToServerAsync: udpClient is null, skipping seq {Seq}", packetSequence);
                    return;
                }

                await client.SendAsync(replyBuffer.GetPacketMemory()).ConfigureAwait(false);
            }
            catch (ObjectDisposedException)
            {
                Log.Information("SendReplyToServerAsync: Socket disposed for seq {Seq}", packetSequence);
            }
            catch (Exception ex)
            {
                Log.Warning("SendReplyToServerAsync error seq {Seq}: {Error}", packetSequence, ex.Message);
            }
        }

        private void OnSendReply(PacketSequence packetSequence)
        {
            if (packetSequence == LoginPacketSequence)
            {
                isConnected = true;
                SessionInfo.SessionState = SessionState.Connected;
                _ = Task.Run(StartServerAliveCheck);

                recvProcessingChannel.Writer.TryWrite(() =>
                {
                    try { OnConnected(); }
                    catch (Exception ex) { Log.Error("OnConnected failed: {Error}", ex.Message); }
                });

                Log.Information("Connected to server. SessionId={Id}", SessionInfo.SessionId);
            }

            if (Interlocked.Read(ref lastSendSequence) < packetSequence)
                return;

            bufferStore.RemoveSendBuffer(packetSequence);
        }

        private async Task RetransmissionAsync()
        {
            using var timer = new PeriodicTimer(TimeSpan.FromMilliseconds(RetransmissionWakeUpMs));
            try
            {
                while (await timer.WaitForNextTickAsync(CancellationToken.Token).ConfigureAwait(false))
                {
                    if (bufferStore.GetSendBufferCount() == 0)
                        continue;

                    var nowMs = CommonFunc.GetNowMs();
                    var sendPacketInfos = bufferStore.GetAllSendPacketInfos();

                    foreach (var info in sendPacketInfos.Where(p => p.IsRetransmissionTime(nowMs)))
                    {
                        if (info.IsExceedMaxRetransmissionCount())
                        {
                            if (!bufferStore.ContainsPacket(info.PacketSequence))
                            {
                                Log.Debug("RetransmissionAsync: seq={Seq} already ACKed, skipping",
                                    info.PacketSequence);
                                continue;
                            }

                            Log.Warning("Max retransmission exceeded for seq={Seq}, disconnecting...",
                                info.PacketSequence);
                            await DisconnectAsync().ConfigureAwait(false);
                            return;
                        }

                        info.RefreshSendPacketInfo(nowMs);

                        var client = udpClient;
                        if (client == null)
                        {
                            Log.Information("RetransmissionAsync: udpClient is null, stopping");
                            return;
                        }

                        try
                        {
                            await client.SendAsync(info.SentBuffer.GetPacketMemory()).ConfigureAwait(false);
                        }
                        catch (ObjectDisposedException)
                        {
                            Log.Information("RetransmissionAsync: Socket disposed, stopping");
                            return;
                        }
                        catch (SocketException ex)
                        {
                            Log.Warning("RetransmissionAsync: SocketException seq={Seq}: {Error}",
                                info.PacketSequence, ex.Message);
                            return;
                        }
                    }
                }
            }
            catch (OperationCanceledException) { Log.Information("RetransmissionAsync: Cancelled"); }
        }

        private async Task DisconnectAsync()
        {
            isConnected = false;
            SessionInfo.SessionState = SessionState.Disconnecting;

            try
            {
                var packet = BuildDisconnectPacket();
                await udpClient.SendAsync(packet.GetPacketMemory()).ConfigureAwait(false);
            }
            catch (Exception ex)
            {
                Log.Warning("DisconnectAsync: Failed to send disconnect packet - {Error}", ex.Message);
            }

            Cleanup();
        }

        private NetBuffer BuildDisconnectPacket()
        {
            var seq = Interlocked.Increment(ref lastSendSequence);
            var buffer = new NetBuffer(64);
            buffer.BuildCorePacket(PacketType.DisconnectType, seq);

            lock (aesGcmLock)
            {
                Debug.Assert(SessionInfo.AesGcm != null);
                NetBuffer.EncodePacket(
                    SessionInfo.AesGcm, buffer, seq,
                    PacketDirection.ClientToServer, SessionInfo.SessionSalt, isCorePacket: true);
            }

            return buffer;
        }

        private async Task StartServerAliveCheck()
        {
            PacketSequence prev = 0;
            using var timer = new PeriodicTimer(TimeSpan.FromSeconds(15));
            try
            {
                while (await timer.WaitForNextTickAsync(CancellationToken.Token).ConfigureAwait(false))
                {
                    if (!isConnected) break;

                    PacketSequence curr;
                    lock (expectedRecvSequenceLock) { curr = expectedRecvSequence; }

                    if (prev != curr) { prev = curr; continue; }

                    Log.Warning("No response from server, disconnecting...");
                    await DisconnectAsync().ConfigureAwait(false);
                    break;
                }
            }
            catch (OperationCanceledException) { Log.Information("StartServerAliveCheck: Cancelled"); }
        }

        private void Cleanup()
        {
            if (Interlocked.CompareExchange(ref isDisposed, 1, 0) != 0)
                return;

            var capturedSessionId = SessionInfo.SessionId;

            isConnected = false;
            OnDisconnected();

            recvProcessingChannel.Writer.TryComplete();

            CancellationToken.Cancel();
            CancellationToken.Dispose();

            udpClient.Close();
            udpClient = null!;

            SessionInfo.SessionState = SessionState.Disconnected;
            SessionInfo.SessionId = 0;
            SessionInfo.SessionKey = [];
            SessionInfo.SessionSalt = [];

            lock (aesGcmLock)
            {
                if (SessionInfo.AesGcm != null)
                {
                    SessionInfo.AesGcm.Dispose();
                    SessionInfo.AesGcm = null;
                }
            }

            TargetServerInfo.ServerIp = string.Empty;
            TargetServerInfo.ServerPort = 0;
            Interlocked.Exchange(ref lastSendSequence, 0);
            lock (expectedRecvSequenceLock) { expectedRecvSequence = 0; }

            holdingPacketStore.Clear();
            bufferStore.Clear();

            OnSessionDisconnected?.Invoke(capturedSessionId);
        }
    }
}
