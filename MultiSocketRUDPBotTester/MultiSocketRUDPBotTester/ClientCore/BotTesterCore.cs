using ClientCore;

namespace MultiSocketRUDPBotTester.ClientCore
{
    public sealed class BotTesterCore
    {
        public static BotTesterCore Instance = new();

        private SessionGetter sessionGetter = new SessionGetter();
        private Dictionary<SessionIdType, RUDPSession> sessionMap = new();

        public void StartBotTest(UInt16 numOfBot)
        {

        }

        public void StopBotTest()
        { 
        }

        public UInt16 GetActiveBotCount()
        {
            return 0;
        }
    }
}
