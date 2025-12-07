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

    public class SessionBrokerResponse
    {
        public ConnectResultCode resultCode { get; set; }
        public string serverIp { get; set; }
        public ushort serverPort { get; set; }
        public ushort sessionId { get; set; }
        public string sessionKey { get; set; }
    }

    public class SessionInfo
    {
        public SessionIdType sessionId { get; set; }
        public string sessionKey { get; set; }
        public SessionState sessionState { get; set; }
        public string serverIp { get; set; }
        public ushort serverPort { get; set; }
    }

    public class RUDPSession
    {
        public RUDPSession(byte[] sessionInfoStream)
        {
            MakeSessionInfo(sessionInfoStream);
        }

        public SessionInfo SessionInfo { get; private set; } = new()
        {
            sessionState = SessionState.Disconnected,
        };

        private UdpClient udpClient = null!;
        private IPEndPoint serverEndPoint = null!;

        private PacketSequence lastSendSequence = 0;
        private PacketSequence expectedRecvSequence = 0;

        private HashSet<PacketSequence> holdingSequences = [];
        private object holdingSequencesLock = new object();
        private SortedDictionary<PacketSequence, NetBuffer> holdingPackets = new SortedDictionary<PacketSequence, NetBuffer>();
        private object holdingPacketsLock = new object();

        private bool isConnected = false;

        private void MakeSessionInfo(byte[] sessionInfoStream)
        {
            // sessionInfoStream to session info
            // If session info is invalid, throw exception
        }

        public SessionIdType GetSessionId()
        {
            return SessionInfo.sessionId;
        }
        
        private static SessionBrokerResponse ParseSessionBrokerResponse(byte[] data, int length)
        {
            var buffer = new NetBuffer();

            var encodedData = new byte[length];
            Array.Copy(data, encodedData, length);

            if (!buffer.Decode(encodedData))
            {
                throw new Exception("Failed to decode session broker response");
            }

            var response = new SessionBrokerResponse
            {
                resultCode = (ConnectResultCode)buffer.ReadByte()
            };

            if (response.resultCode != ConnectResultCode.SUCCESS)
            {
                return response;
            }

            response.serverIp = buffer.ReadString();
            response.serverPort = buffer.ReadUShort();
            response.sessionId = buffer.ReadUShort();
            response.sessionKey = buffer.ReadString();

            return response;
        }

        public void Disconnect()
        {
            Cleanup();
        }

        public async Task<bool> SendPacket(NetBuffer packetBuffer, PacketId packetId, PacketType packetType = PacketType.SEND_TYPE)
        {
            packetBuffer.InsertPacketType(packetType);
            packetBuffer.InsertPacketSequence(++lastSendSequence);
            packetBuffer.InsertPacketId(packetId);

            packetBuffer.Encode();
            await SendPacket(new SendPacketInfo(packetBuffer));

            return true;
        }

        private async Task<bool> SendConnectPacket(NetBuffer packetBuffer)
        {
            await SendPacket(new SendPacketInfo(MakeConnectPacket()));
            return true;
        }

        private async Task<bool> SendPacket(SendPacketInfo sendPacketInfo)
        {
            sendPacketInfo.RefreshSendPacketInfo(CommonFunc.GetNowMs());
            await udpClient.SendAsync(sendPacketInfo.SentBuffer.GetPacketBuffer(), sendPacketInfo.SentBuffer.GetLength(), serverEndPoint);
            return true;
        }

        private NetBuffer MakeConnectPacket()
        {
            var buffer = new NetBuffer();
            buffer.BuildConnectPacket(SessionInfo.sessionId, SessionInfo.sessionKey);

            return buffer;
        }

        private async Task ReceiveAsync(CancellationToken cancellationToken)
        {
            try
            {
                while (!cancellationToken.IsCancellationRequested)
                {
                    var result = await udpClient.ReceiveAsync(cancellationToken);
                    ProcessReceivedPacket(result.Buffer);
                }
            }
            catch (OperationCanceledException) { }
            catch (ObjectDisposedException) { }
            catch (Exception ex)
            {
                if (cancellationToken.IsCancellationRequested == false)
                {
                    Log.Error("Cancellation request failed with {}", ex.ToString());
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

        private void Cleanup()
        {
        }
    }
}