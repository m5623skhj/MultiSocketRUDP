using MultiSocketRUDPBotTester.Buffer;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Data;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading.Tasks;

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
        public ushort sessionId { get; set; }
        public string sessionKey { get; set; }
        public SessionState sessionState { get; set; }
        public string serverIp { get; set; }
        public ushort serverPort { get; set; }
    }

    class RUDPSession
    {
        public SessionInfo sessionInfo { get; private set; }
        private UdpClient udpClient;
        private IPEndPoint serverEndPoint;

        private PacketSequence lastSendSequence;
        private PacketSequence expectedRecvSequence;

        private HashSet<PacketSequence> holdingSequences = new HashSet<PacketSequence>();
        private object holdingSequencesLock = new object();

        private bool isConnected = false;

        public RUDPSession()
        {
            sessionInfo = new SessionInfo
            {
                sessionState = SessionState.Disconnected,
            };
            lastSendSequence = 0;
            expectedRecvSequence = 0;
        }

        public async Task<bool> GetSessionInfoFromServerAsync(string sessionBrokerIp, ushort sessionBrokerPort)
        {
            sessionInfo.sessionState = SessionState.Connecting;

            using var sessionGetter = new TcpClient();
            await sessionGetter.ConnectAsync(sessionBrokerIp, sessionBrokerPort);

            var stream = sessionGetter.GetStream();

            var buffer = new byte[1024];
            var recvTask = await stream.ReadAsync(buffer, 0, buffer.Length);

            int bytesRead = recvTask;
            if (bytesRead == 0)
            {
                return false;
            }

            var response = ParseSessionBrokerResponse(buffer, bytesRead);
            if (response.resultCode != ConnectResultCode.SUCCESS)
            {
                sessionInfo.sessionState = SessionState.Disconnected;
                return false;
            }

            sessionInfo.sessionId = response.sessionId;
            sessionInfo.sessionKey = response.sessionKey;
            sessionInfo.serverIp = response.serverIp;
            sessionInfo.serverPort = response.serverPort;

            return true;
        }

        private SessionBrokerResponse ParseSessionBrokerResponse(byte[] data, int length)
        {
            var buffer = new NetBuffer();

            var encodedData = new byte[length];
            Array.Copy(data, encodedData, length);

            if (!buffer.Decode(encodedData))
            {
                throw new Exception("Failed to decode session broker response");
            }

            var response = new SessionBrokerResponse();
            response.resultCode = (ConnectResultCode)buffer.ReadByte();

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

        public async Task<bool> ConnectToServerAsync()
        {
            udpClient = new UdpClient();
            serverEndPoint = new IPEndPoint(IPAddress.Parse(sessionInfo.serverIp), sessionInfo.serverPort);

            var connectPacket = MakeConnectPacket();
            if (connectPacket == null)
            {
                return false;
            }
            await SendConnectPacket(connectPacket);

            return true;
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
            await udpClient.SendAsync(sendPacketInfo.sendedBuffer.GetPacketBuffer(), sendPacketInfo.sendedBuffer.GetLength(), serverEndPoint);

            return true;
        }

        private NetBuffer MakeConnectPacket()
        {
            var buffer = new NetBuffer();
            buffer.BuildConnectPacket(sessionInfo.sessionId, sessionInfo.sessionKey);

            return buffer;
        }

        private async Task ReceiveAsync(CancellationToken cancellationToken)
        {
            try
            {
                while (!cancellationToken.IsCancellationRequested)
                {
                    var result = await udpClient.ReceiveAsync();
                    ProcessReceivedPacket(result.Buffer);
                }
            }
            catch (OperationCanceledException) { }
            catch (ObjectDisposedException) { }
            catch (Exception ex)
            {
                if (cancellationToken.IsCancellationRequested == false)
                {
                    // log
                }
            }
        }

        private void ProcessReceivedPacket(byte[] data)
        {
            var buffer = new NetBuffer();
            if (buffer.Decode(data) == false)
            {
                // log
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
                        // log
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
            sessionInfo.sessionState = SessionState.Disconnecting;

            var disconnectPacket = BuildDisconnectPacket();
            await udpClient.SendAsync(disconnectPacket.GetPacketBuffer(), disconnectPacket.GetLength(), serverEndPoint);
            udpClient.Close();

            Cleanup();

            sessionInfo.sessionState = SessionState.Disconnected;
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