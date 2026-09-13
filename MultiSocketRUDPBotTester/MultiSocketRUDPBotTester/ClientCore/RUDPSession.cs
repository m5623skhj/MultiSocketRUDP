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

    public enum SessionDisconnectReason
    {
        None = 0,
        Manual = 1,
        RetransmissionLimit = 2,
        ServerUnresponsive = 3
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
        public PacketSequence Sequence { get; set; }
        public PacketId PacketId { get; set; }
        public NetBuffer Buffer { get; set; } = null!;
        public PacketType PacketType { get; set; }
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

        /// <summary>
        /// 현재 holdingPackets 컬렉션에 보류 중인 패킷의 수를 안전하게 반환합니다.
        /// 이 작업은 스레드 안전을 위해 holdingPacketsLock을 사용하여 잠금을 수행합니다.
        /// 실패 조건: 없습니다.
        /// 상태 변화: 이 함수는 내부 컬렉션의 상태를 변경하지 않습니다.
        /// Side Effect: 없습니다.
        /// </summary>
        /// <returns>보류 중인 패킷의 총 수입니다.</returns>
        public int GetCount()
        {
            lock (holdingPacketsLock)
            {
                return holdingPackets.Count;
            }
        }

        /// <summary>
        /// 현재 holdingPackets 컬렉션에 있는 패킷들의 첫 번째와 마지막 PacketSequence를 안전하게 가져오려고 시도합니다.
        /// 이 작업은 스레드 안전을 위해 holdingPacketsLock을 사용하여 잠금을 수행합니다.
        /// 실패 조건: holdingPackets 컬렉션이 비어있는 경우 false를 반환하고, out 매개변수는 기본값으로 설정됩니다.
        /// 상태 변화: out 매개변수인 firstSequence와 lastSequence에 PacketSequence 값을 설정합니다.
        /// Side Effect: 없습니다.
        /// </summary>
        /// <param name="firstSequence">메서드가 반환될 때, 컬렉션의 첫 번째 PacketSequence가 포함됩니다 (컬렉션이 비어있지 않은 경우).</param>
        /// <param name="lastSequence">메서드가 반환될 때, 컬렉션의 마지막 PacketSequence가 포함됩니다 (컬렉션이 비어있지 않은 경우).</param>
        /// <returns>컬렉션에 패킷이 있으면 true, 그렇지 않으면 false입니다.</returns>
        public bool TryGetRange(out PacketSequence firstSequence, out PacketSequence lastSequence)
        {
            lock (holdingPacketsLock)
            {
                if (holdingPackets.Count == 0)
                {
                    firstSequence = default;
                    lastSequence = default;
                    return false;
                }

                firstSequence = holdingPackets.First().Key;
                lastSequence = holdingPackets.Last().Key;
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
        private const int MaxIpv4DatagramPayloadSize = 65507;
        private const int ContentPacketAdditionalSize = sizeof(byte) + sizeof(ulong) + sizeof(uint) + CryptoHelper.AuthTagSize;
        private const int RetransmissionWakeUpMs = 16;
        private const long AckDelayRetransmissionThreshold = 1;
        private const long AckRemovalDelayLogThresholdMs = 5;

        public SessionInfo SessionInfo { get; } = new() { SessionState = SessionState.Disconnected };
        public TargetServerInfo TargetServerInfo { get; } = new();

        private UdpClient udpClient = null!;
        private PacketSequence lastSendSequence = 0;
        private PacketSequence lastUnreliableSendSequence;
        private readonly UnreliablePacketQueue unreliableSendQueue = new();
        private readonly LatestPacketSequence unreliableReceiveSequence = new();
        private long authenticatedReceiveCount;

        private readonly Lock aesGcmLock = new();

        private readonly ReceivePacketOrderer receivePacketOrderer = new();
        private readonly BufferStore bufferStore = new();

        private volatile PacketLossSimulator? packetLossSimulator;
        private long retransmissionTimeoutMs = 20;
        private long retransmissionMaxCount = 16;

        public CancellationTokenSource CancellationToken = new();

        private volatile bool isConnected;
        private int isDisposed;
        private int disconnectReason;

        public Action<SessionIdType>? OnSessionDisconnected { get; set; }

        private readonly Channel<Action> recvProcessingChannel =
            Channel.CreateUnbounded<Action>(
                new UnboundedChannelOptions
                {
                    SingleReader = true,
                    SingleWriter = true
                });

        private readonly Channel<Action> unreliableProcessingChannel = Channel.CreateBounded<Action>(
            new BoundedChannelOptions(ProtocolConstants.UnreliableQueueCapacity)
            {
                FullMode = BoundedChannelFullMode.DropOldest,
                SingleReader = true,
                SingleWriter = true
            });
        private readonly Channel<bool> receiveWakeUp = Channel.CreateBounded<bool>(1);

        protected abstract void OnRecvPacket(PacketId packetId, NetBuffer buffer);
        protected virtual bool TryHandleRecvFastPath(PacketId packetId, NetBuffer buffer) => false;
        protected virtual void OnRttPacketReceived(PacketId packetId, long inReceiveTimestamp) { }
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
            var response = SessionBrokerResponseParser.Parse(data);
            TargetServerInfo.ServerIp = response.ServerIp;
            TargetServerInfo.ServerPort = response.ServerPort;
            SessionInfo.SessionId = response.SessionId;
            SessionInfo.SessionKey = response.SessionKey;
            SessionInfo.SessionSalt = response.SessionSalt;

            SessionInfo.AesGcm = new AesGcm(SessionInfo.SessionKey, 16);
            SessionInfo.SessionState = SessionState.Connecting;

            udpClient = new UdpClient();
            udpClient.Connect(new IPEndPoint(
                IPAddress.Parse(TargetServerInfo.ServerIp), TargetServerInfo.ServerPort));

            _ = Task.Run(ReceiveAsync);
            _ = Task.Run(PacketProcessorAsync);
            _ = Task.Run(RetransmissionAsync);
            _ = Task.Run(SendUnreliablePacketsAsync);
            _ = Task.Run(SendConnectPacketAsync);
        }

        public void Disconnect() => Cleanup(SessionDisconnectReason.Manual);

        public SessionDisconnectReason GetDisconnectReason()
        {
            return (SessionDisconnectReason)Volatile.Read(ref disconnectReason);
        }

        /// <summary>
        /// 소켓 경계에서의 양방향 패킷 손실 시뮬레이션을 설정합니다.
        /// inLossRate가 0 이하면 시뮬레이션을 비활성화합니다.
        /// 핸드세이크가 손실로 오염되지 않도록 연결 완료 이후에 호출해야 합니다.
        /// </summary>
        public void SetPacketLossSimulation(double inLossRate, int inSeed)
        {
            if (inLossRate <= 0.0)
            {
                packetLossSimulator?.SetEnabled(false);
                packetLossSimulator = null;
                return;
            }

            var simualator = new PacketLossSimulator(inLossRate, inSeed);
            simualator.SetEnabled(true);
            packetLossSimulator = simualator;
        }

        /// <summary>
        /// 이후 생성되는 송신 패킷에 적용할 재전송 간격과 최대 횟수를 설정합니다.
        /// 이미 송신 대기열에 들어간 패킷의 설정은 변경하지 않습니다.
        /// </summary>
        public void ConfigureRetransmission(
            int inRetransmissionTimeoutMs,
            int inRetransmissionMaxCount)
        {
            if (inRetransmissionTimeoutMs <= 0)
            {
                throw new ArgumentOutOfRangeException(nameof(inRetransmissionTimeoutMs));
            }
            if (inRetransmissionMaxCount <= 0)
            {
                throw new ArgumentOutOfRangeException(nameof(inRetransmissionMaxCount));
            }

            Interlocked.Exchange(ref retransmissionTimeoutMs, inRetransmissionTimeoutMs);
            Interlocked.Exchange(ref retransmissionMaxCount, inRetransmissionMaxCount);
        }

        private async Task SendDatagramAsync(UdpClient client, ReadOnlyMemory<byte> datagram)
        {
            if (packetLossSimulator?.ShouldDropSendingDatagram() == true)
            {
                return;
            }

            await client.SendAsync(datagram).ConfigureAwait(false);
        }

        public async Task SendPacket(
            NetBuffer packetBuffer,
            PacketId packetId,
            PacketType packetType = PacketType.SendType)
        {
            if (packetType == PacketType.UnreliableSendType)
            {
                if (!SendUnreliablePacket(packetBuffer, packetId))
                    throw new InvalidOperationException("The session is not connected or the packet exceeds the UDP size limit.");
                return;
            }

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

            await SendPacketInternal(CreateSendPacketInfo(packetBuffer, sequence)).ConfigureAwait(false);
        }

        /// <summary>
        /// 비신뢰성 패킷을 큐에 수용하면 true입니다. 실제 전달이나 ACK를 보장하지 않습니다.
        /// 연결이 종료되었거나 최종 크기가 IPv4 UDP 한도를 넘으면 버퍼를 변경하지 않고 false를 반환합니다.
        /// 번호 발급부터 큐 삽입까지 보호하여 동시 송신 순서와 종료 시 암호화 자원 수명을 보장합니다.
        /// </summary>
        public bool SendUnreliablePacket(NetBuffer packetBuffer, PacketId packetId)
        {
            lock (aesGcmLock)
            {
                if (!isConnected || Volatile.Read(ref isDisposed) != 0 || SessionInfo.AesGcm == null)
                    return false;

                // 입력 버퍼에는 헤더가 이미 있습니다. 타입·번호·ID·태그를 추가한 최종 크기를 검사합니다.
                if (packetBuffer.GetLength() > MaxIpv4DatagramPayloadSize - ContentPacketAdditionalSize)
                    return false;

                var sequence = ++lastUnreliableSendSequence;
                packetBuffer.InsertPacketType(PacketType.UnreliableSendType);
                packetBuffer.InsertPacketSequence(sequence);
                packetBuffer.InsertPacketId(packetId);
                NetBuffer.EncodePacket(SessionInfo.AesGcm, packetBuffer, sequence,
                    PacketDirection.ClientToServerUnreliable, SessionInfo.SessionSalt, isCorePacket: false);

                // 큐가 원본 버퍼와 독립된 데이터를 소유합니다.
                return unreliableSendQueue.Enqueue(packetBuffer.GetPacketBuffer());
            }
        }

        private async Task SendUnreliablePacketsAsync()
        {
            try
            {
                await foreach (var packet in unreliableSendQueue.ReadAllAsync(CancellationToken.Token)
                    .ConfigureAwait(false))
                {
                    if (!isConnected || Volatile.Read(ref isDisposed) != 0)
                        break;
                    try
                    {
                        await SendDatagramAsync(udpClient, packet).ConfigureAwait(false);
                    }
                    catch (SocketException ex) when (ex.SocketErrorCode == SocketError.MessageSize)
                    {
                        // 소켓이 더 작은 한도를 적용하더라도 해당 패킷만 버리고 송신 작업을 계속합니다.
                        Log.Warning("Unreliable packet exceeds socket size limit: {Length}", packet.Length);
                    }
                }
            }
            catch (OperationCanceledException) { }
            catch (ObjectDisposedException) { }
            catch (Exception ex)
            {
                Log.Warning("SendUnreliablePacketsAsync failed: {Error}", ex.Message);
                Cleanup(SessionDisconnectReason.Manual);
            }
            finally
            {
                unreliableSendQueue.Clear();
            }
        }

        private async Task<bool> SendPacketInternal(SendPacketInfo sendPacketInfo)
        {
            sendPacketInfo.InitializeSendTimestamp(CommonFunc.GetNowMs());
            bufferStore.EnqueueSendBuffer(sendPacketInfo);

            await SendDatagramAsync(udpClient, sendPacketInfo.SentBuffer.GetPacketMemory()).ConfigureAwait(false);

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

                if (!await SendPacketInternal(CreateSendPacketInfo(buffer, LoginPacketSequence))
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

        private SendPacketInfo CreateSendPacketInfo(
            NetBuffer inBuffer,
            PacketSequence inPacketSequence)
        {
            return new SendPacketInfo(
                inBuffer,
                inPacketSequence,
                Interlocked.Read(ref retransmissionTimeoutMs),
                Interlocked.Read(ref retransmissionMaxCount));
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

                    if (packetLossSimulator?.ShouldDropReceivedDatagram() == true)
                    {
                        continue;
                    }

                    try
                    {
                        await ProcessReceivedStreamAsync(result.Buffer).ConfigureAwait(false);
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
                await foreach (var signal in receiveWakeUp.Reader.ReadAllAsync(CancellationToken.Token)
                    .ConfigureAwait(false))
                {
                    // 채널마다 하나씩 처리하여 비신뢰성 트래픽이 연결 콜백과 신뢰성 처리를 막지 않게 합니다.
                    while (!CancellationToken.IsCancellationRequested)
                    {
                        var hasReliable = recvProcessingChannel.Reader.TryRead(out var reliable);
                        var hasUnreliable = unreliableProcessingChannel.Reader.TryRead(out var unreliable);
                        if (!hasReliable && !hasUnreliable)
                            break;
                        ExecuteReceivedAction(reliable);
                        if (!CancellationToken.IsCancellationRequested)
                            ExecuteReceivedAction(unreliable);
                    }
                }
            }
            catch (OperationCanceledException)
            {
                Log.Information("PacketProcessorAsync: Cancelled");
            }
            finally
            {
                while (recvProcessingChannel.Reader.TryRead(out _)) { }
                while (unreliableProcessingChannel.Reader.TryRead(out _)) { }
            }
        }

        private static void ExecuteReceivedAction(Action? action)
        {
            try { action?.Invoke(); }
            catch (Exception ex)
            {
                Log.Error("PacketProcessorAsync: action failed: {Error}", ex.Message);
            }
        }

        private void QueueReceivedAction(Action action, bool unreliable = false)
        {
            var writer = unreliable ? unreliableProcessingChannel.Writer : recvProcessingChannel.Writer;
            if (writer.TryWrite(action))
                receiveWakeUp.Writer.TryWrite(true);
        }

        /// <summary>
        /// 네이티브 클라이언트와 동일하게 하나의 UDP datagram 안에 합쳐진 여러 패킷을 헤더 기준으로 분리합니다.
        /// </summary>
        private async Task ProcessReceivedStreamAsync(byte[] data)
        {
            var offset = 0;
            while (offset + HeaderSize <= data.Length)
            {
                if (!DatagramFramer.TryGetPacketSize(data, offset, out var packetSize))
                {
                    Log.Error(
                        "ProcessReceivedStreamAsync: invalid packet layout offset={Offset} datagramLength={DatagramLength}",
                        offset,
                        data.Length);
                    return;
                }

                if (offset + packetSize > data.Length)
                {
                    Log.Error(
                        "ProcessReceivedStreamAsync: truncated packet offset={Offset} packetSize={PacketSize} datagramLength={DatagramLength}",
                        offset,
                        packetSize,
                        data.Length);
                    return;
                }

                var packetBytes = new byte[packetSize];
                Array.Copy(data, offset, packetBytes, 0, packetSize);
                await ProcessReceivedPacketAsync(packetBytes).ConfigureAwait(false);

                offset += packetSize;
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
            var isCorePacket = packetType != PacketType.SendType && packetType != PacketType.UnreliableSendType;

            PacketDirection direction;
            switch (packetType)
            {
                case PacketType.UnreliableSendType:
                    direction = PacketDirection.ServerToClientUnreliable;
                    break;
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
            DecodePacketFailureDetails decodeFailureDetails = default;
            lock (aesGcmLock)
            {
                decoded = NetBuffer.DecodePacket(
                    SessionInfo.AesGcm, buffer, isCorePacket, SessionInfo.SessionSalt, direction, out decodeFailureDetails);
            }

            if (!decoded)
            {
                Log.Error(
                    "DecodePacket failed (type={Type}, reason={Reason}, sequence={Sequence}, direction={Direction}, isCorePacket={IsCorePacket}, packetLength={PacketLength}, headerPayloadSize={HeaderPayloadSize}, bodySize={BodySize}, authTagOffset={AuthTagOffset}, lastSendSequence={LastSendSequence}, outstandingSendBuffers={OutstandingSendBuffers})",
                    packetType,
                    decodeFailureDetails.Reason,
                    decodeFailureDetails.PacketSequence,
                    decodeFailureDetails.Direction,
                    decodeFailureDetails.IsCorePacket,
                    decodeFailureDetails.PacketLength,
                    decodeFailureDetails.HeaderPayloadSize,
                    decodeFailureDetails.BodySize,
                    decodeFailureDetails.AuthTagOffset,
                    Interlocked.Read(ref lastSendSequence),
                    bufferStore.GetSendBufferCount());
                return;
            }

            Interlocked.Increment(ref authenticatedReceiveCount);
            var packetSequence = buffer.ReadULong();
            var packetId = PacketId.InvalidPacketId;
            if (!isCorePacket)
            {
                packetId = (PacketId)buffer.ReadUInt();
            }

            if (packetType == PacketType.UnreliableSendType &&
                (!isConnected || !unreliableReceiveSequence.TryAccept(packetSequence)))
                return;

            OnRttPacketReceived(packetId, Stopwatch.GetTimestamp());

            switch (packetType)
            {
                case PacketType.UnreliableSendType:
                    if (!TryHandleRecvFastPath(packetId, buffer))
                        QueueReceivedAction(() => OnRecvPacket(packetId, buffer), unreliable: true);
                    break;
                case PacketType.HeartbeatType:
                case PacketType.SendType:
                    await SendReplyToServerAsync(packetSequence).ConfigureAwait(false);

                    var packetsToProcess = CollectPacketsToProcess(packetSequence, packetId, buffer, packetType);
                    foreach (var (sequence, pid, buf, type) in packetsToProcess)
                    {
                        if (type == PacketType.HeartbeatType)
                        {
                            continue;
                        }

                        var capturedSequence = sequence;
                        var capturedPid = pid;
                        var capturedBuf = buf;
                        if (TryHandleRecvFastPath(capturedPid, capturedBuf))
                        {
                            continue;
                        }

                        QueueReceivedAction(() =>
                        {
                            OnRecvPacket(capturedPid, capturedBuf);
                        });
                    }
                    break;

                case PacketType.SendReplyType:
                    OnSendReply(packetSequence);
                    break;
            }
        }

        private List<(PacketSequence, PacketId, NetBuffer, PacketType)> CollectPacketsToProcess(
            PacketSequence packetSequence,
            PacketId packetId,
            NetBuffer buffer,
            PacketType packetType)
        {
            return receivePacketOrderer.Collect(packetSequence, packetId, buffer, packetType);
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

                await SendDatagramAsync(client, replyBuffer.GetPacketMemory()).ConfigureAwait(false);
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
            var nowMs = CommonFunc.GetNowMs();

            if (packetSequence == LoginPacketSequence)
            {
                lock (aesGcmLock)
                {
                    if (Volatile.Read(ref isDisposed) != 0 ||
                        SessionInfo.SessionState != SessionState.Connecting)
                        return;

                    isConnected = true;
                    SessionInfo.SessionState = SessionState.Connected;
                    var token = CancellationToken.Token;
                    _ = Task.Run(() => StartServerAliveCheck(token));
                    QueueReceivedAction(() =>
                    {
                        if (Volatile.Read(ref isDisposed) != 0) return;
                        try { OnConnected(); }
                        catch (Exception ex) { Log.Error("OnConnected failed: {Error}", ex.Message); }
                    });
                }

                Log.Information("Connected to server. SessionId={Id}", SessionInfo.SessionId);
            }

            if (Interlocked.Read(ref lastSendSequence) < packetSequence)
                return;

            var ackedPacketInfo = bufferStore.GetSendBuffer(packetSequence);
            if (ackedPacketInfo != null)
            {
                ackedPacketInfo.MarkAckReceived(nowMs);
            }

            if (ackedPacketInfo != null &&
                ackedPacketInfo.GetRetransmissionCount() >= AckDelayRetransmissionThreshold)
            {
                Log.Information(
                    "[ClientRetransmissionAck] seq={Sequence} retransmissions={RetransmissionCount} outstandingSendBuffers={OutstandingSendBuffers}",
                    packetSequence,
                    ackedPacketInfo.GetRetransmissionCount(),
                    bufferStore.GetSendBufferCount());
            }

            var removedPacketInfo = bufferStore.RemoveAndGetSendBuffer(packetSequence);
            if (removedPacketInfo == null)
            {
                return;
            }

            removedPacketInfo.MarkRemoved(nowMs);

            var ackRemovalDelayMs = nowMs - (ulong)removedPacketInfo.GetAckReceivedTimestampMs();
            if (ackRemovalDelayMs >= AckRemovalDelayLogThresholdMs)
            {
                Log.Information(
                    "[ClientAckRemovalDelay] seq={Sequence} ackRemovalDelayMs={AckRemovalDelayMs} retransmissions={RetransmissionCount} outstandingSendBuffers={OutstandingSendBuffers}",
                    packetSequence,
                    ackRemovalDelayMs,
                    removedPacketInfo.GetRetransmissionCount(),
                    bufferStore.GetSendBufferCount());
            }
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
                        if (info.HasAckReceived())
                        {
                            var ackReceivedTimestampMs = info.GetAckReceivedTimestampMs();
                            Log.Information(
                                "[ClientAckRace] seq={Sequence} ackAgeMs={AckAgeMs} retransmissions={RetransmissionCount} outstandingSendBuffers={OutstandingSendBuffers}",
                                info.PacketSequence,
                                nowMs - (ulong)ackReceivedTimestampMs,
                                info.GetRetransmissionCount(),
                                bufferStore.GetSendBufferCount());
                            continue;
                        }

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
                            await DisconnectAsync(SessionDisconnectReason.RetransmissionLimit)
                                .ConfigureAwait(false);
                            return;
                        }

                        info.RefreshSendPacketInfo(nowMs);
                        Log.Information(
                            "[ClientRetransmission] seq={Sequence} retransmissions={RetransmissionCount} outstandingSendBuffers={OutstandingSendBuffers} createdAgeMs={CreatedAgeMs} lastSendAgeMs={LastSendAgeMs}",
                            info.PacketSequence,
                            info.GetRetransmissionCount(),
                            bufferStore.GetSendBufferCount(),
                            nowMs - (ulong)info.GetCreatedTimestampMs(),
                            nowMs - (ulong)info.GetSendTimestampMs());

                        var client = udpClient;
                        if (client == null)
                        {
                            Log.Information("RetransmissionAsync: udpClient is null, stopping");
                            return;
                        }

                        try
                        {
                            await SendDatagramAsync(client, info.SentBuffer.GetPacketMemory()).ConfigureAwait(false);
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

        private async Task DisconnectAsync(SessionDisconnectReason inReason)
        {
            lock (aesGcmLock)
            {
                if (Volatile.Read(ref isDisposed) != 0 || SessionInfo.SessionState == SessionState.Disconnecting)
                    return;
                isConnected = false;
                SessionInfo.SessionState = SessionState.Disconnecting;
            }

            try
            {
                var packet = BuildDisconnectPacket();
                await SendDatagramAsync(udpClient, packet.GetPacketMemory()).ConfigureAwait(false);
            }
            catch (Exception ex)
            {
                Log.Warning("DisconnectAsync: Failed to send disconnect packet - {Error}", ex.Message);
            }

            Cleanup(inReason);
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

        private async Task StartServerAliveCheck(System.Threading.CancellationToken token)
        {
            var prev = Interlocked.Read(ref authenticatedReceiveCount);
            using var timer = new PeriodicTimer(TimeSpan.FromSeconds(15));
            try
            {
                while (await timer.WaitForNextTickAsync(token).ConfigureAwait(false))
                {
                    if (!isConnected) break;

                    var curr = Interlocked.Read(ref authenticatedReceiveCount);

                    if (prev != curr) { prev = curr; continue; }

                    Log.Warning("No response from server, disconnecting...");
                    await DisconnectAsync(SessionDisconnectReason.ServerUnresponsive)
                        .ConfigureAwait(false);
                    break;
                }
            }
            catch (OperationCanceledException) { Log.Information("StartServerAliveCheck: Cancelled"); }
        }

        private void Cleanup(SessionDisconnectReason inReason)
        {
            if (Interlocked.CompareExchange(ref isDisposed, 1, 0) != 0)
                return;

            var capturedSessionId = SessionInfo.SessionId;
            Volatile.Write(ref disconnectReason, (int)inReason);

            lock (aesGcmLock)
            {
                isConnected = false;
                unreliableSendQueue.Complete();
            }
            recvProcessingChannel.Writer.TryComplete();
            unreliableProcessingChannel.Writer.TryComplete();
            receiveWakeUp.Writer.TryComplete();
            CancellationToken.Cancel();

            OnDisconnected();

            CancellationToken.Dispose();

            udpClient.Close();
            udpClient = null!;

            SessionInfo.SessionState = SessionState.Disconnected;
            SessionInfo.SessionId = 0;
            lock (aesGcmLock)
            {
                SessionInfo.SessionKey = [];
                SessionInfo.SessionSalt = [];
                if (SessionInfo.AesGcm != null)
                {
                    SessionInfo.AesGcm.Dispose();
                    SessionInfo.AesGcm = null;
                }
            }

            TargetServerInfo.ServerIp = string.Empty;
            TargetServerInfo.ServerPort = 0;
            Interlocked.Exchange(ref lastSendSequence, 0);
            receivePacketOrderer.Clear();
            bufferStore.Clear();

            OnSessionDisconnected?.Invoke(capturedSessionId);
        }
    }
}
