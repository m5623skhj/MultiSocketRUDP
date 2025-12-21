using System;
using System.Collections.Generic;
using System.Linq;
using System.Security.RightsManagement;
using System.Text;
using System.Threading.Tasks;
using ClientCore;
using MultiSocketRUDPBotTester.Bot;
using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.Contents.Client.Action;

namespace MultiSocketRUDPBotTester.Contents.Client
{
    public partial class Client : RUDPSession
    {
        private ActionGraph actionGraph = new();

        public Client(byte[] sessionInfoStream)
            : base(sessionInfoStream)
        {
            RegisterPacketHandlers();
        }

        public void SetActionGraph(ActionGraph graph)
        {
            actionGraph = graph;
        }

        protected override void OnRecvPacket(NetBuffer buffer)
        {
            const PacketId packetId = 0;
            if (packetHandlerDictionary.TryGetValue(packetId, out var action))
            {
                action.Execute(buffer);
            }

            actionGraph.TriggerEvent(this, TriggerType.OnPacketReceived, packetId);
        }
    }
}
