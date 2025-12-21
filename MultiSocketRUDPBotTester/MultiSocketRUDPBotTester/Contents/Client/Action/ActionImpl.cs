using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using MultiSocketRUDPBotTester.Buffer;

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
