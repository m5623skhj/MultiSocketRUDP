using System.Diagnostics;
using MultiSocketRUDPBotTester.Buffer;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using Serilog;

namespace ClientCore
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
        SUCCESS = 0,
        SERVER_FULL = 1,
        ALREADY_CONNECTED_SESSION = 2,
        CREATE_SOCKET_FAILED = 3,
        RIO_INIT_FAILED = 4,
        DO_RECV_FAILED = 5
    }
    
    public class SessionInfo
    {
        public static readonly int SessionKeySize = 16;

        public SessionIdType SessionId { get; set; }
        public byte[] SessionKey { get; set; } = new byte[16];
        public string SessionSalt { get; set; } = "";
        public SessionState SessionState { get; set; }
        public AesGcm? AesGcm { get; set; }
    }

    public class TargetServerInfo
    {
        public string ServerIp { get; set; } = "";
        public ushort ServerPort { get; set; } = 0;
    }

    public class HoldingPacketStore
    {
        public readonly HashSet<PacketSequence> HoldingSequences = [];
        private readonly Lock holdingSequencesLock = new();
        public readonly SortedDictionary<PacketSequence, NetBuffer> HoldingPackets = new();
        private readonly Lock holdingPacketsLock = new();

        public void AddHoldingPacket(PacketSequence sequence, NetBuffer buffer)
        {
            lock (holdingPacketsLock)
            {
                HoldingPackets.Add(sequence, buffer);
            }

            lock (holdingSequencesLock)
            {
                HoldingSequences.Add(sequence);
            }
        }

        public void RemoveHoldingPacket(PacketSequence sequence)
        {
            lock (holdingSequencesLock)
            {
                HoldingSequences.Remove(sequence);
            }

            lock (holdingPacketsLock)
            {
                HoldingPackets.Remove(sequence);
            }
        }

        public void Clear()
        {
            lock (holdingSequencesLock)
            {
                HoldingSequences.Clear();
            }

            lock (holdingPacketsLock)
            {
                HoldingPackets.Clear();
            }
        }
    }

    public abstract class RUDPSession
    {
        protected RUDPSession(byte[] sessionInfoStream)
        {
            MakeSessionInfo(sessionInfoStream);
        }

        public SessionInfo SessionInfo { get; } = new()
        {
            SessionState = SessionState.Disconnected,
        };

        public TargetServerInfo TargetServerInfo { get; } = new();

        private UdpClient udpClient = null!;

        private PacketSequence lastSendSequence = 1;
        private PacketSequence expectedRecvSequence = 0;
        private const int RetransmissionWakeUpMs = 30;

        private readonly HoldingPacketStore holdingPacketStore = new();
        public CancellationTokenSource CancellationToken = new();
        private readonly BufferStore bufferStore = new();

        private volatile bool isConnected;

        protected abstract void OnRecvPacket(NetBuffer buffer);

        protected virtual void OnConnected() {}
        protected virtual void OnDisconnected() {}

        private void MakeSessionInfo(byte[] sessionInfoStream)
        {
            ParseSessionBrokerResponse(sessionInfoStream);
        }

        public SessionIdType GetSessionId()
        {
            return SessionInfo.SessionId;

        }
        public bool IsConnected()
        {
            return isConnected;
        }
        
        private void ParseSessionBrokerResponse(byte[] data)
        {
            var buffer = new NetBuffer();
            buffer.WriteBytes(data);

            var resultCode = (ConnectResultCode)buffer.ReadByte();
            if (resultCode != ConnectResultCode.SUCCESS)
            {
                throw new Exception($"Session broker response error: {resultCode}");
            }

            TargetServerInfo.ServerIp = buffer.ReadString();
            TargetServerInfo.ServerPort = buffer.ReadUShort();
            SessionInfo.SessionId = buffer.ReadUShort();
            SessionInfo.SessionKey = buffer.ReadBytes(SessionInfo.SessionKeySize);
            SessionInfo.SessionSalt = buffer.ReadString();
            SessionInfo.AesGcm = new AesGcm(SessionInfo.SessionKey, SessionInfo.SessionKeySize);

            udpClient = new UdpClient();
            udpClient.Connect(new IPEndPoint(IPAddress.Parse(TargetServerInfo.ServerIp), TargetServerInfo.ServerPort));

            SessionInfo.SessionState = SessionState.Connecting;
            _ = ReceiveAsync();
            _ = RetransmissionAsync();
            var result =  SendConnectPacket();
            if (!result.Result)
            {
                Log.Error("SendConnectPacket() failed. SessionId {}", SessionInfo.SessionId);
            }
        }

        public void Disconnect()
        {
            Cleanup();
        }

        public async Task SendPacket(NetBuffer packetBuffer, PacketId packetId, PacketType packetType = PacketType.SEND_TYPE)
        {
            var sequence = ++lastSendSequence;
            packetBuffer.InsertPacketType(packetType);
            packetBuffer.InsertPacketSequence(sequence);
            packetBuffer.InsertPacketId(packetId);

            Debug.Assert(SessionInfo.AesGcm != null);
            NetBuffer.EncodePacket(SessionInfo.AesGcm, packetBuffer, sequence, PacketDirection.CLIENT_TO_SERVER, SessionInfo.SessionSalt, false);

            await SendPacket(new SendPacketInfo(packetBuffer));
        }

        private async Task<bool> SendPacket(SendPacketInfo sendPacketInfo)
        {
            sendPacketInfo.RefreshSendPacketInfo(CommonFunc.GetNowMs());
            bufferStore.EnqueueSendBuffer(sendPacketInfo);

            await udpClient.SendAsync(sendPacketInfo.SentBuffer.GetPacketBuffer(), sendPacketInfo.SentBuffer.GetLength());
            return true;
        }

        private async Task<bool> SendConnectPacket()
        {
            return await SendPacket(new SendPacketInfo(MakeConnectPacket()));
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
            catch (OperationCanceledException)
            {
                Log.Information("Cancel by cancellationToken");
            }
            catch (ObjectDisposedException) { }
            catch (Exception exception)
            {
                if (CancellationToken.Token.IsCancellationRequested == false)
                {
                    Log.Error("Cancellation request failed with {}", exception.ToString());
                }
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
            var isCorePacket = packetType == PacketType.SEND_TYPE;
            PacketDirection direction;
            switch (packetType)
            {
                case PacketType.HEARTBEAT_TYPE:
                case PacketType.SEND_TYPE:
                {
                    direction = PacketDirection.SERVER_TO_CLIENT;
                    break;
                }
                case PacketType.SEND_REPLY_TYPE:
                {
                    direction = PacketDirection.SERVER_TO_CLIENT_REPLY;
                    break;
                }
                default:
                {
                    return;
                }
            }

            if (!NetBuffer.DecodePacket(SessionInfo.AesGcm
                    , buffer
                    , isCorePacket
                    , SessionInfo.SessionKey
                    , SessionInfo.SessionSalt
                    , direction))
            {
                Log.Error("Failed to decode received packet");
                return;
            }

            var packetSequence = buffer.ReadULong();
            switch (packetType)
            {
                case PacketType.HEARTBEAT_TYPE:
                {
                    SendReplyToServer(packetSequence);
                    break;
                }
                case PacketType.SEND_TYPE:
                {
                    SendReplyToServer(packetSequence);
                    OnRecvPacket(buffer);
                    break;
                }
                case PacketType.SEND_REPLY_TYPE:
                {
                    OnSendReply(packetSequence);
                    break;
                }
            }
        }

        private void SendReplyToServer(PacketSequence packetSequence)
        {
            var replyBuffer = new NetBuffer();
            replyBuffer.WriteByte((byte)PacketType.SEND_REPLY_TYPE);
            replyBuffer.WriteULong(packetSequence);


            Debug.Assert(SessionInfo.AesGcm != null);
            NetBuffer.EncodePacket(SessionInfo.AesGcm, replyBuffer, packetSequence, PacketDirection.CLIENT_TO_SERVER_REPLY, SessionInfo.SessionSalt, true);

            _ = udpClient.SendAsync(replyBuffer.GetPacketBuffer(), replyBuffer.GetLength());
        }

        private void OnSendReply(PacketSequence packetSequence)
        {
            if (packetSequence < expectedRecvSequence)
            {
                return;
            }

            if (packetSequence == 0)
            {
                isConnected = true;
                _ = StartServerAliveCheck();
                OnConnected();
            }

            holdingPacketStore.RemoveHoldingPacket(packetSequence);
            bufferStore.RemoveSendBuffer(packetSequence);
        }

        private async Task RetransmissionAsync()
        {
            using var timer = new PeriodicTimer(TimeSpan.FromMilliseconds(RetransmissionWakeUpMs));
            try
            {
                while (await timer.WaitForNextTickAsync(CancellationToken.Token))
                {
                    var nowMs = CommonFunc.GetNowMs();
                    var sendPacketInfos = bufferStore.GetAllSendPacketInfos();
                    foreach (var sendPacketInfo in sendPacketInfos.Where(sendPacketInfo => sendPacketInfo.IsRetransmissionTime(nowMs)))
                    {
                        if (sendPacketInfo.IsExceedMaxRetransmissionCount())
                        {
                            Log.Warning("Max retransmission count exceeded, disconnecting...");
                            await DisconnectAsync();
                            return;
                        }

                        sendPacketInfo.RefreshSendPacketInfo(nowMs);
                        await udpClient.SendAsync(sendPacketInfo.SentBuffer.GetPacketBuffer(), sendPacketInfo.SentBuffer.GetLength());
                    }
                }
            }
            catch (OperationCanceledException)
            {
                Log.Information("Retransmission check cancelled");
            }
        }

        private async Task DisconnectAsync()
        {
            isConnected = false;
            SessionInfo.SessionState = SessionState.Disconnecting;

            var disconnectPacket = BuildDisconnectPacket();
            await udpClient.SendAsync(disconnectPacket.GetPacketBuffer(), disconnectPacket.GetLength());
            udpClient.Close();

            Cleanup();

            SessionInfo.SessionState = SessionState.Disconnected;
        }

        private NetBuffer BuildDisconnectPacket()
        {
            var netBuffer = new NetBuffer();
            netBuffer.WriteByte((byte)PacketType.DISCONNECT_TYPE);

            Debug.Assert(SessionInfo.AesGcm != null);
            NetBuffer.EncodePacket(SessionInfo.AesGcm, netBuffer, ++lastSendSequence, PacketDirection.CLIENT_TO_SERVER, SessionInfo.SessionSalt, true);

            return netBuffer;
        }

        private async Task StartServerAliveCheck()
        {
            PacketSequence beforeReceivedSequence = 0;
            using var timer = new PeriodicTimer(TimeSpan.FromSeconds(15));
            try
            {
                while (await timer.WaitForNextTickAsync(CancellationToken.Token))
                {
                    if (!isConnected)
                    {
                        break;
                    }

                    if (beforeReceivedSequence != expectedRecvSequence)
                    {
                        beforeReceivedSequence = expectedRecvSequence;
                        continue;
                    }

                    Log.Warning("No response from server, disconnecting...");
                    await DisconnectAsync();
                    break;
                }
            }
            catch (OperationCanceledException)
            {
                Log.Information("StartServerAliveCheck cancelled");
            }
        }

        private void Cleanup()
        {
            OnDisconnected();
            
            udpClient.Close();
            CancellationToken.Cancel();
            CancellationToken.Dispose();

            udpClient = null!;
            SessionInfo.SessionState = SessionState.Disconnected;
            SessionInfo.SessionId = 0;
            SessionInfo.SessionKey = [];
            SessionInfo.SessionSalt = string.Empty;
            if (SessionInfo.AesGcm != null)
            {
                SessionInfo.AesGcm.Dispose();
                SessionInfo.AesGcm = null;
            }
            TargetServerInfo.ServerIp = string.Empty;
            TargetServerInfo.ServerPort = 0;
            lastSendSequence = 0;
            expectedRecvSequence = 0;
            holdingPacketStore.Clear();

            isConnected = false;
        }
    }
}