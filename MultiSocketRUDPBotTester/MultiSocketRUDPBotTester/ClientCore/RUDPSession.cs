using MultiSocketRUDPBotTester.Buffer;
using System.Net;
using System.Net.Sockets;
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
        public SessionIdType sessionId { get; set; }
        public string sessionKey { get; set; } = "";
        public string sessionSalt { get; set; } = "";
        public SessionState sessionState { get; set; }
    }

    public class TargetServerInfo
    {
        public string serverIp { get; set; } = "";
        public ushort serverPort { get; set; } = 0;
    }

    public class RUDPSession
    {
        public RUDPSession(byte[] sessionInfoStream)
        {
            MakeSessionInfo(sessionInfoStream);
        }

        public SessionInfo SessionInfo { get; } = new()
        {
            sessionState = SessionState.Disconnected,
        };

        public TargetServerInfo TargetServerInfo { get; } = new();

        private UdpClient udpClient = null!;
        private readonly IPEndPoint serverEndPoint = null!;

        private PacketSequence lastSendSequence = 1;
        private PacketSequence expectedRecvSequence = 0;

        private HashSet<PacketSequence> holdingSequences = [];
        private object holdingSequencesLock = new();
        private SortedDictionary<PacketSequence, NetBuffer> holdingPackets = new();
        private object holdingPacketsLock = new();
        public CancellationToken cancellationToken;

        private bool isConnected = false;

        private void MakeSessionInfo(byte[] sessionInfoStream)
        {
            ParseSessionBrokerResponse(sessionInfoStream);
        }

        public SessionIdType GetSessionId()
        {
            return SessionInfo.sessionId;

        }
        public bool IsConnected()
        {
            return isConnected;
        }
        
        private void ParseSessionBrokerResponse(byte[] data)
        {
            var buffer = new NetBuffer();
            if (!buffer.Decode(data))
            {
                throw new Exception("Failed to decode session broker response");
            }

            var resultCode = (ConnectResultCode)buffer.ReadByte();
            if (resultCode != ConnectResultCode.SUCCESS)
            {
                throw new Exception($"Session broker response error: {resultCode}");
            }

            TargetServerInfo.serverIp = buffer.ReadString();
            TargetServerInfo.serverPort = buffer.ReadUShort();
            SessionInfo.sessionId = buffer.ReadUShort();
            SessionInfo.sessionKey = buffer.ReadString();
            SessionInfo.sessionSalt = buffer.ReadString();

            SessionInfo.sessionState = SessionState.Connecting;
            _ = ReceiveAsync();
            var result =  SendConnectPacket();
            if (!result.Result)
            {
                Log.Error("SendConnectPacket() failed. SessionId {}", SessionInfo.sessionId);
            }
        }

        public void Disconnect()
        {
            Cleanup();
        }

        public async Task SendPacket(NetBuffer packetBuffer, PacketId packetId, PacketType packetType = PacketType.SEND_TYPE)
        {
            packetBuffer.InsertPacketType(packetType);
            packetBuffer.InsertPacketSequence(++lastSendSequence);
            packetBuffer.InsertPacketId(packetId);

            packetBuffer.Encode();
            await SendPacket(new SendPacketInfo(packetBuffer));
        }

        private async Task<bool> SendPacket(SendPacketInfo sendPacketInfo)
        {
            sendPacketInfo.RefreshSendPacketInfo(CommonFunc.GetNowMs());
            await udpClient.SendAsync(sendPacketInfo.SentBuffer.GetPacketBuffer(), sendPacketInfo.SentBuffer.GetLength(), serverEndPoint);
            return true;
        }

        private async Task<bool> SendConnectPacket()
        {
            return await SendPacket(new SendPacketInfo(MakeConnectPacket()));
        }

        private NetBuffer MakeConnectPacket()
        {
            var buffer = new NetBuffer();
            buffer.BuildConnectPacket(SessionInfo.sessionId);

            return buffer;
        }

        private async Task ReceiveAsync()
        {
            try
            {
                while (!cancellationToken.IsCancellationRequested)
                {
                    var result = await udpClient.ReceiveAsync(cancellationToken);
                    ProcessReceivedPacket(result.Buffer);
                }
            }
            catch (OperationCanceledException)
            {
                Log.Warning("Cancel by cancellationToken");
            }
            catch (ObjectDisposedException) { }
            catch (Exception exception)
            {
                if (cancellationToken.IsCancellationRequested == false)
                {
                    Log.Error("Cancellation request failed with {}", exception.ToString());
                }
            }
        }

        private void ProcessReceivedPacket(byte[] data)
        {
            var buffer = new NetBuffer();
            if (buffer.Decode(data) == false)
            {
                Log.Error("Buffer decode failed");
                return;
            }

            var packetType = (PacketType)buffer.ReadByte();
            var packetSequence = buffer.ReadULong();
            switch (packetType)
            {
                case PacketType.SEND_TYPE:
                case PacketType.HEARTBEAT_TYPE:
                    {
                        // SendReplyToServer(packetSequence);
                    }
                    break;
                case PacketType.SEND_REPLY_TYPE:
                    {
                        OnSendReply(buffer, packetSequence);
                    }
                    break;
                default:
                    {
                        Log.Error("Invalid packet type {}", packetType);
                    }
                    break;
            }
        }

        private void OnSendReply(NetBuffer recvPacket, PacketSequence packetSequence)
        {
            if (packetSequence < expectedRecvSequence)
            {
                return;
            }

            if (packetSequence == 0)
            {
                isConnected = true;
                _ = StartServerAliveCheck();
            }

            // Erase send packet info container by packetSequence
        }

        private async Task DisconnectAsync()
        {
            isConnected = false;
            SessionInfo.sessionState = SessionState.Disconnecting;

            var disconnectPacket = BuildDisconnectPacket();
            await udpClient.SendAsync(disconnectPacket.GetPacketBuffer(), disconnectPacket.GetLength(), serverEndPoint);
            udpClient.Close();

            Cleanup();

            SessionInfo.sessionState = SessionState.Disconnected;
        }

        private NetBuffer BuildDisconnectPacket()
        {
            var netBuffer = new NetBuffer();
            netBuffer.WriteByte((byte)PacketType.DISCONNECT_TYPE);
            netBuffer.WriteULong(0);
            return netBuffer;
        }

        private async Task StartServerAliveCheck()
        {
            PacketSequence beforeReceivedSequence = 0;
            using var timer = new PeriodicTimer(TimeSpan.FromSeconds(15));
            try
            {
                while (await timer.WaitForNextTickAsync(cancellationToken))
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
            catch (OperationCanceledException exception)
            {
                Log.Error("Server alive check throw with {}", exception.ToString());
            }
        }

        private void Cleanup()
        {
            udpClient.Close();
            udpClient = null!;
            SessionInfo.sessionState = SessionState.Disconnected;
            SessionInfo.sessionId = 0;
            SessionInfo.sessionKey = string.Empty;
            SessionInfo.sessionSalt = string.Empty;
            TargetServerInfo.serverIp = string.Empty;
            TargetServerInfo.serverPort = 0;
            lastSendSequence = 0;
            expectedRecvSequence = 0;
            holdingSequences.Clear();
            holdingPackets.Clear();

            isConnected = false;
        }
    }
}