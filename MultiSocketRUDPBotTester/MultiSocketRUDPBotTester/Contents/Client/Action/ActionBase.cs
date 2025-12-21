using MultiSocketRUDPBotTester.Buffer;

namespace MultiSocketRUDPBotTester.Contents.Client.Action
{
    public abstract class ActionBase
    {
        public abstract void Execute(NetBuffer buffer);
    }
}
