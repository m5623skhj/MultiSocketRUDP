using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.Contents.Client;

namespace MultiSocketRUDPBotTester.Bot
{
    public enum ActionNodeType
    {
        SendPacket,
        Wait,
        Loop,
        Conditional,
    }

    public abstract class ActionNodeBase
    {
        public string Name { get; set; } = string.Empty;
        public TriggerCondition? Trigger { get; set; }
        public List<ActionNodeBase> NextNodes { get; set; } = [];

        public abstract void Execute(Client client, NetBuffer? receivedPacket = null);

        public ActionNodeBase AddNext(ActionNodeBase nextNode)
        {
            NextNodes.Add(nextNode);
            return nextNode;
        }

        private static NodeStatsTracker? _statsTracker;
        public static void SetStatsTracker(NodeStatsTracker? tracker) => _statsTracker = tracker;
        internal static NodeStatsTracker? GetStatsTracker() => _statsTracker;

    }
}