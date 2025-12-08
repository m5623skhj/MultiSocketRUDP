using ClientCore;
using Serilog;
using System;

namespace MultiSocketRUDPBotTester.ClientCore
{
    public sealed class BotTesterCore
    {
        public static BotTesterCore Instance = new();

        private SessionGetter sessionGetter = new SessionGetter();
        private Dictionary<SessionIdType, RUDPSession> sessionDictionary = new();
        private readonly Lock sessionDictionaryLock = new();

        private string hostIp = "";
        private UInt16 hostPort = 0;

        public async Task StartBotTest(UInt16 numOfBot)
        {
            for (var i = 0; i < numOfBot; ++i)
            {
                var rudpSession = await GetSessionInfoFromSessionBroker();
                if (rudpSession == null)
                {
                    Log.Error("StartBotTest() failed to get session info from session broker");
                    return;
                }

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

        public UInt16 GetActiveBotCount()
        {
            lock (sessionDictionaryLock)
            {
                return (UInt16)sessionDictionary.Count;
            }
        }

        private async Task<RUDPSession?> GetSessionInfoFromSessionBroker()
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
                    if (totalBytes < GlobalConstants.packetHeaderSize)
                    {
                        continue;
                    }

                    if (payloadLength == 0)
                    {
                        payloadLength = BitConverter.ToUInt16(buffer, GlobalConstants.payloadPosition);
                    }

                    if (totalBytes >= payloadLength + GlobalConstants.packetHeaderSize)
                    {
                        break;
                    }
                }
            }
            catch (Exception e)
            {
                throw new Exception($"GetSessionInfoFromSessionBroker failed with {e.Message}");
            }

            return new RUDPSession(buffer[GlobalConstants.packetHeaderSize..totalBytes]);
        }
    }
}
