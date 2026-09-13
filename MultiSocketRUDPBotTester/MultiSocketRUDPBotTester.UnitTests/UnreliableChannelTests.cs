using System.Buffers.Binary;
using System.Net;
using System.Net.Sockets;
using System.Net.Security;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Diagnostics;
using System.Threading.Channels;
using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.ClientCore;
using MultiSocketRUDPBotTester.Contents.Client;

namespace MultiSocketRUDPBotTester.UnitTests;

public sealed class UnreliableChannelTests
{
    [Fact]
    public async Task DuplicateConnectAckDoesNotRepeatConnectedCallback()
    {
        using var server = new UdpClient(new IPEndPoint(IPAddress.Loopback, 0));
        var session = new ProbeSession(BrokerResponse(((IPEndPoint)server.Client.LocalEndPoint!).Port));
        try
        {
            var peer = await Connect(server, session);
            // Following content runs on the same callback queue, after any repeated connect callback.
            var packets = Packet(PacketType.SendReplyType, 0)
                .Concat(Packet(PacketType.SendReplyType, 0))
                .Concat(Packet(PacketType.SendType, 1, 42)).ToArray();
            await server.SendAsync(packets, peer);
            Assert.Equal(42, await session.Received.Reader.ReadAsync().AsTask().WaitAsync(TimeSpan.FromSeconds(5)));
            Assert.Equal(1, Volatile.Read(ref session.ConnectedCount));
            Assert.True(session.IsConnected());
        }
        finally { session.Disconnect(); }
    }

    [Fact]
    public async Task ConnectAckAlreadyBeingProcessedCannotReconnectDisposedSession()
    {
        using var server = new UdpClient(new IPEndPoint(IPAddress.Loopback, 0));
        var session = new ProbeSession(BrokerResponse(((IPEndPoint)server.Client.LocalEndPoint!).Port));
        try
        {
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
            var login = await server.ReceiveAsync(timeout.Token);
            // The RTT hook runs after authentication and before dispatching the connection ACK.
            session.BeforePacketDispatch = () => session.Disconnect();
            var disconnected = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            session.OnSessionDisconnected = _ => disconnected.TrySetResult();
            await server.SendAsync(Packet(PacketType.SendReplyType, 0), login.RemoteEndPoint);
            await disconnected.Task.WaitAsync(timeout.Token);
            // Re-enter the ACK handler synchronously to also check its final post-cleanup state.
            typeof(RudpSession).GetMethod("OnSendReply", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)!
                .Invoke(session, new object[] { 0UL });
            Assert.False(session.IsConnected());
            Assert.Equal(SessionState.Disconnected, session.SessionInfo.SessionState);
            Assert.Equal(0, Volatile.Read(ref session.ConnectedCount));
        }
        finally { session.Disconnect(); }
    }

    [Theory]
    [InlineData(65507, true)]
    [InlineData(65508, false)]
    [InlineData(65534, false)]
    public async Task DatagramSizeBoundaryDoesNotDisconnectSession(int encodedSize, bool expectedAccepted)
    {
        using var server = new UdpClient(new IPEndPoint(IPAddress.Loopback, 0));
        var session = new ProbeSession(BrokerResponse(((IPEndPoint)server.Client.LocalEndPoint!).Port));
        try
        {
            var peer = await Connect(server, session);
            var buffer = new NetBuffer(65536);
            buffer.ReserveHeader();
            buffer.WriteBytes(new byte[encodedSize - 34]); // header + type + sequence + id + tag
            var originalLength = buffer.GetLength();
            Assert.Equal(expectedAccepted, session.SendUnreliablePacket(buffer, PacketId.TestPacketReq));
            if (expectedAccepted)
                Assert.Equal(encodedSize, (await Receive(server)).Length);
            else
                Assert.Equal(originalLength, buffer.GetLength());

            Assert.True(session.SendUnreliablePacket(Body(42), PacketId.TestPacketReq));
            var received = Decode(await Receive(server), PacketDirection.ClientToServerUnreliable);
            Assert.Equal(42, received.Body);
            Assert.Equal(expectedAccepted ? 2UL : 1UL, received.Sequence);
            await session.SendPacket(Body(43), PacketId.TestPacketReq);
            var reliable = Decode(await Receive(server), PacketDirection.ClientToServer);
            Assert.Equal(43, reliable.Body);
            await server.SendAsync(Packet(PacketType.SendReplyType, reliable.Sequence), peer);
            Assert.True(session.IsConnected());
            Assert.Equal(SessionDisconnectReason.None, session.GetDisconnectReason());
        }
        finally { session.Disconnect(); }
    }

    [Fact]
    public async Task StaleUnreliableResponsesPreserveRttTimestamps()
    {
        using var server = new UdpClient(new IPEndPoint(IPAddress.Loopback, 0));
        var client = new Client(BrokerResponse(((IPEndPoint)server.Client.LocalEndPoint!).Port));
        try
        {
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
            var login = await server.ReceiveAsync(timeout.Token);
            await server.SendAsync(Packet(PacketType.SendReplyType, 0), login.RemoteEndPoint);
            await client.WaitUntilConnectedAsync(5000, timeout.Token);
            client.EnableRttMode();
            client.BeginRttSample(Stopwatch.GetTimestamp());
            var pong = client.WaitForPongAsync(5000, timeout.Token);
            await server.SendAsync(Packet(PacketType.UnreliableSendType, 100, packetId: PacketId.Pong), login.RemoteEndPoint);
            Assert.NotNull(await pong);
            Assert.True(client.TryCreateRttTraceSnapshot(Stopwatch.GetTimestamp(), out var before));

            var stalePackets = Packet(PacketType.UnreliableSendType, 99, packetId: PacketId.Pong)
                .Concat(Packet(PacketType.UnreliableSendType, 100, packetId: PacketId.Pong))
                .Concat(Packet(PacketType.HeartbeatType, 1)).ToArray();
            await server.SendAsync(stalePackets, login.RemoteEndPoint);
            // 마지막 하트비트 ACK로 앞선 두 패킷의 수신 처리가 끝났음을 확인합니다.
            var ack = Decode(await Receive(server), PacketDirection.ClientToServerReply);
            Assert.Equal(1UL, ack.Sequence);
            Assert.True(client.TryCreateRttTraceSnapshot(Stopwatch.GetTimestamp(), out var after));
            Assert.Equal(before.SocketReceiveTimestamp, after.SocketReceiveTimestamp);
            Assert.Equal(before.FastPathTimestamp, after.FastPathTimestamp);

            pong = client.WaitForPongAsync(5000, timeout.Token);
            await server.SendAsync(Packet(PacketType.UnreliableSendType, 101, packetId: PacketId.Pong), login.RemoteEndPoint);
            Assert.NotNull(await pong);
            Assert.True(client.TryCreateRttTraceSnapshot(Stopwatch.GetTimestamp(), out var latest));
            Assert.True(latest.SocketReceiveTimestamp > before.SocketReceiveTimestamp);
            Assert.True(latest.FastPathTimestamp >= latest.SocketReceiveTimestamp);
        }
        finally { client.Disconnect(); }
    }

    [Fact]
    public async Task SessionGetterSendsBigEndianVersionAfterTlsHandshake()
    {
        using var key = RSA.Create(2048);
        var request = new CertificateRequest("CN=localhost", key, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
        using var generated = request.CreateSelfSigned(DateTimeOffset.UtcNow.AddMinutes(-1), DateTimeOffset.UtcNow.AddDays(1));
        // Windows Schannel은 TLS 서버 인증에 ephemeral 개인 키를 사용할 수 없습니다.
        using var certificate = X509CertificateLoader.LoadPkcs12(generated.Export(X509ContentType.Pfx), null);
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        var serverTask = Task.Run(async () =>
        {
            using var client = await listener.AcceptTcpClientAsync(timeout.Token);
            using var tls = new SslStream(client.GetStream());
            await tls.AuthenticateAsServerAsync(new SslServerAuthenticationOptions
            {
                ServerCertificate = certificate,
                EnabledSslProtocols = SslProtocols.Tls12
            }, timeout.Token);
            var version = new byte[4];
            await tls.ReadExactlyAsync(version, timeout.Token);
            Assert.Equal(new byte[] { 0, 0, 0, 2 }, version);
        });
        using var getter = new SessionGetter();
        await Task.WhenAll(serverTask,
            getter.ConnectAsync("127.0.0.1", ((IPEndPoint)listener.LocalEndpoint).Port)).WaitAsync(timeout.Token);
    }

    [Fact]
    public async Task FullQueueAcceptsNewPacketAndDropsOldestUnsentPacket()
    {
        var queue = new UnreliablePacketQueue(3);
        for (byte value = 1; value <= 4; value++)
            Assert.True(queue.Enqueue(new byte[] { value }));
        queue.Complete();
        Assert.False(queue.Enqueue(new byte[] { 5 }));

        var remaining = new List<byte>();
        await foreach (var packet in queue.ReadAllAsync(CancellationToken.None))
            remaining.Add(packet.Span[0]);
        Assert.Equal(new byte[] { 2, 3, 4 }, remaining);
    }

    [Theory]
    [InlineData(0UL)]
    [InlineData(100UL)]
    public void FirstSequenceIsAcceptedAndOlderOrDuplicateSequencesAreDropped(ulong first)
    {
        var sequence = new LatestPacketSequence();
        Assert.True(sequence.TryAccept(first));
        Assert.False(sequence.TryAccept(first));
        Assert.True(sequence.TryAccept(first + 10));
        Assert.False(sequence.TryAccept(first + 9));
        Assert.True(sequence.TryAccept(first + 11));
    }

    [Fact]
    public void EveryDirectionHasADistinctNonceForTheSameSequence()
    {
        var nonces = new HashSet<string>();
        foreach (var direction in Enum.GetValues<PacketDirection>().Where(value => value != PacketDirection.Invalid))
        {
            var nonce = new byte[12];
            CryptoHelper.WriteNonce(nonce, Salt, 42, direction);
            Assert.True(nonces.Add(Convert.ToHexString(nonce)));
        }
        Assert.Equal(6, nonces.Count);
    }

    [Fact]
    public void BrokerRejectsOldAndUnknownProtocolVersions()
    {
        var response = BrokerResponse(5000);
        Assert.Throws<InvalidDataException>(() => SessionBrokerResponseParser.Parse(response[4..]));
        BinaryPrimitives.WriteUInt32LittleEndian(response, 1);
        Assert.Throws<InvalidDataException>(() => SessionBrokerResponseParser.Parse(response));
        BinaryPrimitives.WriteUInt32LittleEndian(response, 3);
        Assert.Throws<InvalidDataException>(() => SessionBrokerResponseParser.Parse(response));
    }

    [Fact]
    public async Task UnreliableSendUsesIndependentSequenceAndIsNotRetransmitted()
    {
        using var server = new UdpClient(new IPEndPoint(IPAddress.Loopback, 0));
        var session = new ProbeSession(BrokerResponse(((IPEndPoint)server.Client.LocalEndPoint!).Port));
        try
        {
            var peer = await Connect(server, session);
            Assert.True(session.SendUnreliablePacket(Body(10), PacketId.TestPacketReq));
            var unreliable = Decode(await Receive(server), PacketDirection.ClientToServerUnreliable);
            Assert.Equal(PacketType.UnreliableSendType, unreliable.Type);
            Assert.Equal(1UL, unreliable.Sequence);
            Assert.Equal(10, unreliable.Body);

            await session.SendPacket(Body(20), PacketId.TestPacketReq);
            var reliable = Decode(await Receive(server), PacketDirection.ClientToServer);
            Assert.Equal(PacketType.SendType, reliable.Type);
            Assert.Equal(1UL, reliable.Sequence);
            await server.SendAsync(Packet(PacketType.SendReplyType, 1), peer);

            // 신뢰성 송신이었다면 재전송·종료가 발생할 만큼 대기합니다.
            using var timeout = new CancellationTokenSource(500);
            try
            {
                while (true)
                {
                    var data = (await server.ReceiveAsync(timeout.Token)).Buffer;
                    Assert.NotEqual((byte)PacketType.UnreliableSendType, data[5]);
                }
            }
            catch (OperationCanceledException) when (timeout.IsCancellationRequested) { }
            Assert.True(session.IsConnected());
        }
        finally { session.Disconnect(); }
        Assert.False(session.SendUnreliablePacket(Body(30), PacketId.TestPacketReq));
    }

    [Fact]
    public async Task MixedReceiveDropsStaleAndUnauthenticatedUnreliableWithoutAcksOrReliableGaps()
    {
        using var server = new UdpClient(new IPEndPoint(IPAddress.Loopback, 0));
        var session = new ProbeSession(BrokerResponse(((IPEndPoint)server.Client.LocalEndPoint!).Port));
        try
        {
            var peer = await Connect(server, session);
            var corrupt = Packet(PacketType.UnreliableSendType, 10000, 999);
            corrupt[^1] ^= 1;
            var packets = new[]
            {
                Packet(PacketType.SendType, 2, 2),
                Packet(PacketType.UnreliableSendType, 100, 100),
                Packet(PacketType.UnreliableSendType, 100, 100),
                Packet(PacketType.UnreliableSendType, 99, 99),
                corrupt,
                Packet(PacketType.UnreliableSendType, 101, 101),
                Packet(PacketType.SendType, 1, 1),
                Packet(PacketType.HeartbeatType, 3),
                Packet(PacketType.SendType, 4, 4)
            };
            await server.SendAsync(packets.SelectMany(bytes => bytes).ToArray(), peer);

            var received = new List<int>();
            for (var index = 0; index < 5; index++)
                received.Add(await session.Received.Reader.ReadAsync().AsTask().WaitAsync(TimeSpan.FromSeconds(5)));
            Assert.Equal(new[] { 100, 101 }, received.Where(value => value >= 100));
            Assert.Equal(new[] { 1, 2, 4 }, received.Where(value => value < 100));
            Assert.False(session.Received.Reader.TryRead(out _));

            var acks = new List<ulong>();
            for (var index = 0; index < 4; index++)
            {
                var ack = Decode(await Receive(server), PacketDirection.ClientToServerReply);
                Assert.Equal(PacketType.SendReplyType, ack.Type);
                acks.Add(ack.Sequence);
            }
            Assert.Equal(new ulong[] { 2, 1, 3, 4 }, acks);
            using var timeout = new CancellationTokenSource(100);
            await Assert.ThrowsAnyAsync<OperationCanceledException>(async () => await server.ReceiveAsync(timeout.Token));
        }
        finally { session.Disconnect(); }
    }

    [Fact]
    public async Task SlowCallbackKeepsOnlyNewestUnreliablePacketsWithinCapacity()
    {
        using var server = new UdpClient(new IPEndPoint(IPAddress.Loopback, 0));
        using var release = new ManualResetEventSlim();
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var marker = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var session = new ProbeSession(BrokerResponse(((IPEndPoint)server.Client.LocalEndPoint!).Port));
        session.BeforeReceive = value =>
        {
            if (value != 0) return;
            entered.TrySetResult();
            if (!release.Wait(TimeSpan.FromSeconds(10)))
                throw new TimeoutException("Test callback was not released.");
        };
        session.Marker = () => marker.TrySetResult();
        try
        {
            var peer = await Connect(server, session);
            await server.SendAsync(Packet(PacketType.SendType, 1), peer);
            await entered.Task.WaitAsync(TimeSpan.FromSeconds(5));

            var packets = Enumerable.Range(1, 100)
                .SelectMany(value => Packet(PacketType.UnreliableSendType, (ulong)value, value))
                .Concat(Packet(PacketType.SendType, 2, packetId: PacketId.Pong)).ToArray();
            await server.SendAsync(packets, peer);
            // 같은 수신 루프의 마지막 표식이 도착하면 앞선 100개 패킷의 큐 삽입이 끝난 상태입니다.
            await marker.Task.WaitAsync(TimeSpan.FromSeconds(5));
            release.Set();
            Assert.Equal(0, await session.Received.Reader.ReadAsync().AsTask().WaitAsync(TimeSpan.FromSeconds(5)));
            var received = new List<int>();
            for (var index = 0; index < ProtocolConstants.UnreliableQueueCapacity; index++)
                received.Add(await session.Received.Reader.ReadAsync().AsTask().WaitAsync(TimeSpan.FromSeconds(5)));
            Assert.Equal(Enumerable.Range(37, 64), received);
            Assert.False(session.Received.Reader.TryRead(out _));
        }
        finally
        {
            release.Set();
            session.Disconnect();
        }
    }

    private static readonly byte[] Key = Enumerable.Range(1, 16).Select(value => (byte)value).ToArray();
    private static readonly byte[] Salt = Enumerable.Range(160, 16).Select(value => (byte)value).ToArray();

    private static byte[] BrokerResponse(int port)
    {
        using var stream = new MemoryStream();
        using var writer = new BinaryWriter(stream);
        writer.Write(ProtocolConstants.Version);
        writer.Write((byte)ConnectResultCode.Success);
        writer.Write((ushort)9);
        writer.Write("127.0.0.1"u8);
        writer.Write((ushort)port);
        writer.Write((ushort)1);
        writer.Write(Key);
        writer.Write(Salt);
        return stream.ToArray();
    }

    private static NetBuffer Body(int value)
    {
        var buffer = new NetBuffer(64);
        buffer.ReserveHeader();
        buffer.WriteInt(value);
        return buffer;
    }

    private static byte[] Packet(PacketType type, ulong sequence, int value = 0, PacketId packetId = PacketId.TestPacketRes)
    {
        var isCore = type != PacketType.SendType && type != PacketType.UnreliableSendType;
        var buffer = new NetBuffer(64);
        if (isCore)
            buffer.BuildCorePacket(type, sequence);
        else
        {
            buffer = Body(value);
            buffer.InsertPacketType(type);
            buffer.InsertPacketSequence(sequence);
            buffer.InsertPacketId(packetId);
        }
        var direction = type == PacketType.UnreliableSendType ? PacketDirection.ServerToClientUnreliable :
            type == PacketType.SendReplyType ? PacketDirection.ServerToClientReply : PacketDirection.ServerToClient;
        using var aes = new AesGcm(Key, 16);
        NetBuffer.EncodePacket(aes, buffer, sequence, direction, Salt, isCore);
        return buffer.GetPacketBuffer();
    }

    private static (PacketType Type, ulong Sequence, int? Body) Decode(byte[] data, PacketDirection direction)
    {
        var type = (PacketType)data[5];
        var isCore = type != PacketType.SendType && type != PacketType.UnreliableSendType;
        var buffer = new NetBuffer(data.Length);
        buffer.WriteBytes(data);
        using var aes = new AesGcm(Key, 16);
        Assert.True(NetBuffer.DecodePacket(aes, buffer, isCore, Salt, direction));
        buffer.SkipBytes(6);
        var sequence = buffer.ReadULong();
        if (isCore) return (type, sequence, null);
        buffer.ReadUInt();
        return (type, sequence, buffer.ReadInt());
    }

    private static async Task<byte[]> Receive(UdpClient server)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        while (true)
        {
            var result = await server.ReceiveAsync(timeout.Token);
            if ((PacketType)result.Buffer[5] != PacketType.ConnectType)
                return result.Buffer;
        }
    }

    private static async Task<IPEndPoint> Connect(UdpClient server, ProbeSession session)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        var connect = await server.ReceiveAsync(timeout.Token);
        Assert.Equal((byte)PacketType.ConnectType, connect.Buffer[5]);
        await server.SendAsync(Packet(PacketType.SendReplyType, 0), connect.RemoteEndPoint);
        await session.Connected.Task.WaitAsync(timeout.Token);
        return connect.RemoteEndPoint;
    }

    private sealed class ProbeSession(byte[] response) : RudpSession(response)
    {
        public TaskCompletionSource Connected { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
        public Channel<int> Received { get; } = Channel.CreateUnbounded<int>();
        public Action<int>? BeforeReceive { get; set; }
        public Action? Marker { get; set; }
        public Action? BeforePacketDispatch { get; set; }
        public int ConnectedCount;
        protected override void OnConnected()
        {
            Interlocked.Increment(ref ConnectedCount);
            Connected.TrySetResult();
        }
        protected override void OnRttPacketReceived(PacketId packetId, long timestamp) => BeforePacketDispatch?.Invoke();
        protected override void OnRecvPacket(PacketId packetId, NetBuffer buffer)
        {
            var value = buffer.ReadInt();
            BeforeReceive?.Invoke(value);
            Received.Writer.TryWrite(value);
        }
        protected override bool TryHandleRecvFastPath(PacketId packetId, NetBuffer buffer)
        {
            if (packetId != PacketId.Pong || Marker == null) return false;
            Marker();
            return true;
        }
    }
}
