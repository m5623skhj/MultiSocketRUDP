using ClientCore;
using Serilog;

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

        public async void StartBotTest(UInt16 numOfBot)
        {
            try
            {
                var buffer = new byte[1024];
                for (var i = 0; i < numOfBot; ++i)
                {
                    await sessionGetter.ConnectAsync(hostIp, hostPort);
                    var receivedBytes = await sessionGetter.ReceiveAsync(buffer);

                    var rudpSession = new RUDPSession(buffer[..receivedBytes]);
                    lock (sessionDictionaryLock)
                    {
                        sessionDictionary.Add(rudpSession.GetSessionId(), rudpSession);
                    }

                    sessionGetter.Close();
                }
            }
            catch (Exception e)
            {
                Log.Error("StartBotTest() error with {}", e);
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
    }
}
