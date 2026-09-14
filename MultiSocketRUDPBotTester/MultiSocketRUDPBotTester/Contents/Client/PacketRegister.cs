using MultiSocketRUDPBotTester.Contents.Client.Action;

namespace MultiSocketRUDPBotTester.Contents.Client
{
    public partial class Client
    {
        private readonly Dictionary<PacketId, ActionBase> packetHandlerDictionary = new();

        private void RegisterPacketHandlers()
        {
            RegisterGeneratedPacketHandlers();
            // Optional handwritten overrides; regeneration never changes this file.
            packetHandlerDictionary[PacketId.Pong] = new PongAction();
            packetHandlerDictionary[PacketId.TestStringPacketRes] = new TestStringPacketRes();
            packetHandlerDictionary[PacketId.TestPacketRes] = new TestByteArrayPacketRes();
        }
    }
}
