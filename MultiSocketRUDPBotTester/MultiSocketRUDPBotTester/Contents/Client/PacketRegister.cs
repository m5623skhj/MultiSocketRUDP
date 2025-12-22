using MultiSocketRUDPBotTester.Contents.Client.Action;

namespace MultiSocketRUDPBotTester.Contents.Client
{
    public partial class Client
    {
        private readonly Dictionary<PacketId, ActionBase> packetHandlerDictionary = new();

        private void RegisterPacketHandlers()
        {
            packetHandlerDictionary.Add(PacketId.PONG, new PongAction());
            packetHandlerDictionary.Add(PacketId.TEST_STRING_PACKET_RES, new TestStringPacketRes());
            packetHandlerDictionary.Add(PacketId.TEST_PACKET_RES, new TestByteArrayPacketRes());
        }
    }
}
