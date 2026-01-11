using Serilog;
using MultiSocketRUDPBotTester.Bot;
using MultiSocketRUDPBotTester.Contents.Client;

namespace MultiSocketRUDPBotTester.ClientCore
{
    public sealed class BotTesterCore
    {
        public static BotTesterCore Instance = new();

        private readonly SessionGetter sessionGetter = new();
        private readonly Dictionary<SessionIdType, Client> sessionDictionary = new();
        private readonly Lock sessionDictionaryLock = new();

        private string hostIp = "";
        private ushort hostPort = 0;

        private ActionGraph botActionGraph = new();
        private List<NodeVisual>? savedNodeVisuals;

        public void SetConnectionInfo(string targetIp, ushort targetPort)
        {
            if (string.IsNullOrWhiteSpace(targetIp))
            {
                throw new ArgumentException("IP cannot be empty", nameof(targetIp));
            }

            if (targetPort == 0)
            {
                throw new ArgumentException("Port cannot be 0", nameof(targetPort));
            }

            hostIp = targetIp;
            hostPort = targetPort;
        }
        
        public void SetBotActionGraph(ActionGraph graph)
        {
            botActionGraph = graph;
        }

        public void SaveGraphVisuals(List<NodeVisual> nodeVisuals)
        {
            savedNodeVisuals = nodeVisuals;
        }

        public List<NodeVisual>? GetSavedGraphVisuals()
        {
            return savedNodeVisuals;
        }

        public async Task StartBotTest(ushort numOfBot)
        {
            if (string.IsNullOrEmpty(hostIp) || hostPort == 0)
            {
                throw new InvalidOperationException("Connection info not set. Call SetConnectionInfo first.");
            }

            for (var i = 0; i < numOfBot; ++i)
            {
                var rudpSession = await GetSessionInfoFromSessionBroker();
                if (rudpSession == null)
                {
                    Log.Error("StartBotTest() failed to get session info from session broker");
                    return;
                }

                rudpSession.SetActionGraph(botActionGraph);

                lock (sessionDictionaryLock)
                {
                    sessionDictionary.Add(rudpSession.GetSessionId(), rudpSession);
                }

                sessionGetter.Close();
            }
        }

        public void StopBotTest()
        {
            lock (sessionDictionaryLock)
            {
                foreach (var session in sessionDictionary.Values)
                {
                    session.Disconnect();
                }
                sessionDictionary.Clear();
            }
        }

        public int GetActiveBotCount()
        {
            lock (sessionDictionaryLock)
            {
                return sessionDictionary.Values.Count(client => client.IsConnected());
            }
        }

        private async Task<Client?> GetSessionInfoFromSessionBroker()
        {
            var buffer = new byte[1024];
            var totalBytes = 0;
            var payloadLength = 0;
            try
            {
                await sessionGetter.ConnectAsync(hostIp, hostPort);
                while (true)
                {
                    var receivedBytes = await sessionGetter.ReceiveAsync(buffer, totalBytes);
                    if (receivedBytes == 0)
                    {
                        break;
                    }

                    totalBytes += receivedBytes;
                    if (totalBytes < GlobalConstants.PacketHeaderSize)
                    {
                        continue;
                    }

                    if (payloadLength == 0)
                    {
                        payloadLength = BitConverter.ToUInt16(buffer, GlobalConstants.PayloadPosition);
                    }

                    if (totalBytes >= payloadLength + GlobalConstants.PacketHeaderSize)
                    {
                        break;
                    }
                }
            }
            catch (Exception e)
            {
                throw new Exception($"GetSessionInfoFromSessionBroker failed with {e.Message}");
            }

            return new Client(buffer[GlobalConstants.PacketHeaderSize..totalBytes]);
        }
    }
}
