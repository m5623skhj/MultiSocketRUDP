using System.Diagnostics;
using MultiSocketRUDPBotTester.Buffer;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using Serilog;

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

        private const int HeaderSize = 3;

        private const int RetransmissionWakeUpMs = 30;

        public SessionInfo SessionInfo { get; } = new() { SessionState = SessionState.Disconnected };
        public TargetServerInfo TargetServerInfo { get; } = new();

        private UdpClient udpClient = null!;

        private PacketSequence lastSendSequence = 1;

        private readonly Lock expectedRecvSequenceLock = new();
        private PacketSequence expectedRecvSequence;

        private readonly HoldingPacketStore holdingPacketStore = new();
        private readonly BufferStore bufferStore = new();

        public CancellationTokenSource CancellationToken = new();

        private volatile bool isConnected;
        private int isDisposed;

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
            var buffer = new NetBuffer();
            buffer.WriteBytes(data);

            buffer.SkipBytes(HeaderSize);

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
            udpClient.Connect(new IPEndPoint(IPAddress.Parse(TargetServerInfo.ServerIp), TargetServerInfo.ServerPort));

            _ = ReceiveAsync();
            _ = RetransmissionAsync();

            _ = SendConnectPacketAsync();
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

            Debug.Assert(SessionInfo.AesGcm != null);
            NetBuffer.EncodePacket(
                SessionInfo.AesGcm,
                packetBuffer,
                sequence,
                PacketDirection.ClientToServer,
                SessionInfo.SessionSalt,
                isCorePacket: false);

            await SendPacketInternal(new SendPacketInfo(packetBuffer, sequence));
        }

        private async Task<bool> SendPacketInternal(SendPacketInfo sendPacketInfo)
        {
            sendPacketInfo.RefreshSendPacketInfo(CommonFunc.GetNowMs());
            bufferStore.EnqueueSendBuffer(sendPacketInfo);

            await udpClient.SendAsync(
                sendPacketInfo.SentBuffer.GetPacketBuffer(),
                sendPacketInfo.SentBuffer.GetLength());
            return true;
        }

        private async Task SendConnectPacketAsync()
        {
            try
            {
                var buffer = MakeConnectPacket();

                Debug.Assert(SessionInfo.AesGcm != null);
                NetBuffer.EncodePacket(
                    SessionInfo.AesGcm,
                    buffer,
                    LoginPacketSequence,
                    direction: PacketDirection.ClientToServer,
                    sessionSalt: SessionInfo.SessionSalt,
                    isCorePacket: true);

                var success = await SendPacketInternal(new SendPacketInfo(buffer, LoginPacketSequence));
                if (!success)
                    Log.Error("SendConnectPacketAsync() failed. SessionId {Id}", SessionInfo.SessionId);
            }
            catch (Exception ex)
            {
                Log.Error("SendConnectPacketAsync() exception: {Error}", ex.Message);
            }
        }

        private NetBuffer MakeConnectPacket()
        {
            var buffer = new NetBuffer();
            buffer.BuildConnectPacket(SessionInfo.SessionId);
            return buffer;
        }

        private async Task ReceiveAsync()
        {
            try
            {
                while (!CancellationToken.Token.IsCancellationRequested)
                {
                    var result = await udpClient.ReceiveAsync(CancellationToken.Token);
                    ProcessReceivedPacket(result.Buffer);
                }
            }
            catch (OperationCanceledException) { Log.Information("ReceiveAsync: Cancelled"); }
            catch (ObjectDisposedException) { }
            catch (Exception ex)
            {
                if (!CancellationToken.Token.IsCancellationRequested)
                    Log.Error("ReceiveAsync failed: {Error}", ex.ToString());
            }
        }

        private void ProcessReceivedPacket(byte[] data)
        {
            if (SessionInfo.AesGcm == null)
            {
                Log.Error("AesGcm is null, cannot decode packet");
                return;
            }

            var buffer = new NetBuffer();
            buffer.WriteBytes(data);

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
                    Log.Warning("ProcessReceivedPacket: Unknown packet type {Type}", packetType);
                    return;
            }

            if (!NetBuffer.DecodePacket(
                    SessionInfo.AesGcm,
                    buffer,
                    isCorePacket,
                    SessionInfo.SessionSalt,
                    direction))
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
                    SendReplyToServer(packetSequence);
                    break;

                case PacketType.SendType:
                    SendReplyToServer(packetSequence);
                    lock (expectedRecvSequenceLock)
                    {
                        if (packetSequence <= expectedRecvSequence)
                            return;

                        if (packetSequence == expectedRecvSequence + 1)
                        {
                            expectedRecvSequence = packetSequence;
                            OnRecvPacket(packetId, buffer);
                            ProcessHoldingPackets();
                        }
                        else
                        {
                            holdingPacketStore.Add(packetSequence,
                                new HeldPacket { PacketId = packetId, Buffer = buffer });
                        }
                    }
                    break;

                case PacketType.SendReplyType:
                    OnSendReply(packetSequence);
                    break;
            }
        }

        private void ProcessHoldingPackets()
        {
            while (holdingPacketStore.TryGetFirst(out var nextSequence, out var heldPacket))
            {
                if (nextSequence != expectedRecvSequence + 1)
                    break;

                expectedRecvSequence = nextSequence;
                holdingPacketStore.Remove(nextSequence);
                OnRecvPacket(heldPacket.PacketId, heldPacket.Buffer);
            }
        }

        private void SendReplyToServer(PacketSequence packetSequence)
        {
            var replyBuffer = new NetBuffer();
            replyBuffer.WriteByte((byte)PacketType.SendReplyType);
            replyBuffer.WriteULong(packetSequence);

            Debug.Assert(SessionInfo.AesGcm != null);
            NetBuffer.EncodePacket(
                SessionInfo.AesGcm,
                replyBuffer,
                packetSequence,
                PacketDirection.ClientToServerReply,
                SessionInfo.SessionSalt,
                isCorePacket: true);

            _ = udpClient.SendAsync(replyBuffer.GetPacketBuffer(), replyBuffer.GetLength());
        }

        private void OnSendReply(PacketSequence packetSequence)
        {
            if (packetSequence == LoginPacketSequence)
            {
                isConnected = true;
                SessionInfo.SessionState = SessionState.Connected;
                _ = StartServerAliveCheck();
                OnConnected();
                Log.Information("Connected to server. SessionId={Id}", SessionInfo.SessionId);
            }

            if (Interlocked.Read(ref lastSendSequence) < packetSequence)
            {
                return;
            }

            bufferStore.RemoveSendBuffer(packetSequence);
        }

        private async Task RetransmissionAsync()
        {
            using var timer = new PeriodicTimer(TimeSpan.FromMilliseconds(RetransmissionWakeUpMs));
            try
            {
                while (await timer.WaitForNextTickAsync(CancellationToken.Token))
                {
                    if (bufferStore.GetSendBufferCount() == 0) continue;

                    var nowMs = CommonFunc.GetNowMs();
                    var sendPacketInfos = bufferStore.GetAllSendPacketInfos();

                    foreach (var info in sendPacketInfos.Where(p => p.IsRetransmissionTime(nowMs)))
                    {
                        if (info.IsExceedMaxRetransmissionCount())
                        {
                            Log.Warning("Max retransmission count exceeded, disconnecting...");
                            await DisconnectAsync();
                            return;
                        }

                        info.RefreshSendPacketInfo(nowMs);
                        await udpClient.SendAsync(
                            info.SentBuffer.GetPacketBuffer(),
                            info.SentBuffer.GetLength());
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
                await udpClient.SendAsync(packet.GetPacketBuffer(), packet.GetLength());
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
            var buffer = new NetBuffer();
            buffer.WriteByte((byte)PacketType.DisconnectType);
            buffer.WriteULong(seq);

            Debug.Assert(SessionInfo.AesGcm != null);
            NetBuffer.EncodePacket(
                SessionInfo.AesGcm,
                buffer,
                seq,
                PacketDirection.ClientToServer,
                SessionInfo.SessionSalt,
                isCorePacket: true);

            return buffer;
        }

        private async Task StartServerAliveCheck()
        {
            PacketSequence prev = 0;
            using var timer = new PeriodicTimer(TimeSpan.FromSeconds(15));
            try
            {
                while (await timer.WaitForNextTickAsync(CancellationToken.Token))
                {
                    if (!isConnected) break;

                    PacketSequence curr;
                    lock (expectedRecvSequenceLock) { curr = expectedRecvSequence; }

                    if (prev != curr) { prev = curr; continue; }

                    Log.Warning("No response from server, disconnecting...");
                    await DisconnectAsync();
                    break;
                }
            }
            catch (OperationCanceledException) { Log.Information("StartServerAliveCheck: Cancelled"); }
        }

        private void Cleanup()
        {
            if (Interlocked.CompareExchange(ref isDisposed, 1, 0) != 0)
            {
                return;
            }

            isConnected = false;
            OnDisconnected();

            CancellationToken.Cancel();
            CancellationToken.Dispose();

            udpClient.Close();
            udpClient = null!;

            SessionInfo.SessionState = SessionState.Disconnected;
            SessionInfo.SessionId = 0;
            SessionInfo.SessionKey = [];
            SessionInfo.SessionSalt = [];
            if (SessionInfo.AesGcm != null)
            {
                SessionInfo.AesGcm.Dispose();
                SessionInfo.AesGcm = null;
            }

            TargetServerInfo.ServerIp = string.Empty;
            TargetServerInfo.ServerPort = 0;
            Interlocked.Exchange(ref lastSendSequence, 0);
            lock (expectedRecvSequenceLock) { expectedRecvSequence = 0; }

            holdingPacketStore.Clear();
        }
    }
}
