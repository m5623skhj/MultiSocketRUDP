using System;
using System.Collections.Generic;
using System.Linq;
using System.Security.RightsManagement;
using System.Text;
using System.Threading.Tasks;
using ClientCore;
using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.Contents.Client.Action;

namespace MultiSocketRUDPBotTester.Contents.Client
{
    public partial class Client : RUDPSession
    {
        public Client(byte[] sessionInfoStream)
            : base(sessionInfoStream)
        {
            RegisterPacketHandlers();
        }

        protected override void OnRecvPacket(NetBuffer buffer)
        {
            // TODO: Implement packet handling logic
            const PacketId packetId = 0;
            if (packetHandlerDictionary.TryGetValue(packetId, out var action))
            {
                action.Execute(buffer);
            }
        }
    }
}
