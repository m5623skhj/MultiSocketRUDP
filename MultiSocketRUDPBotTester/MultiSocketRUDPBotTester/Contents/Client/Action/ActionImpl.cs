using MultiSocketRUDPBotTester.Buffer;
using Serilog;

namespace MultiSocketRUDPBotTester.Contents.Client.Action
{
    public class PongAction : ActionBase
    {
        public override void Execute(NetBuffer buffer)
        {
        }
    }

    public class TestStringPacketRes : ActionBase
    {
        public override void Execute(NetBuffer buffer)
        {
        }
    }

    public class TestByteArrayPacketRes : ActionBase
    {
        public override void Execute(NetBuffer buffer)
        {
        }
    }
}
