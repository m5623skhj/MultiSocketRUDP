using MultiSocketRUDPBotTester.Contents.Client.Action;

namespace MultiSocketRUDPBotTester.Contents.Client
{
    public partial class Client
    {
        private readonly Dictionary<PacketId, ActionBase> packetHandlerDictionary = new();

        private void RegisterPacketHandlers()
        {
            packetHandlerDictionary.Add(PacketId.Pong, new PongAction());
            packetHandlerDictionary.Add(PacketId.TestStringPacketRes, new TestStringPacketRes());
            packetHandlerDictionary.Add(PacketId.TestPacketRes, new TestByteArrayPacketRes());
        }
    }
}
