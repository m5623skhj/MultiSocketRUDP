using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.Contents.Client;
using Serilog;

namespace MultiSocketRUDPBotTester.Bot
{
    public class ActionGraph
    {
        private readonly Dictionary<TriggerType, List<ActionNodeBase>> triggerNodes = new();
        private readonly Dictionary<PacketId, List<ActionNodeBase>> packetTriggerNodes = new();
        private readonly List<ActionNodeBase> allNodes = [];

        public string Name { get; set; } = "Unnamed Graph";
        public List<ActionNodeBase> GetAllNodes() => [.. allNodes];

        public void AddNode(ActionNodeBase node)
        {
            allNodes.Add(node);
            if (node.Trigger == null)
            {
                return;
            }

            if (!triggerNodes.ContainsKey(node.Trigger.Type))
            {
                triggerNodes[node.Trigger.Type] = [];
            }
            triggerNodes[node.Trigger.Type].Add(node);

            if (node.Trigger.Type != TriggerType.OnPacketReceived || !node.Trigger.PacketId.HasValue)
            {
                return;
            }

            var packetId = node.Trigger.PacketId.Value;
            if (!packetTriggerNodes.ContainsKey(packetId))
            {
                packetTriggerNodes[packetId] = [];
            }

            packetTriggerNodes[packetId].Add(node);
        }

        public void TriggerEvent(Client client, TriggerType triggerType, PacketId? packetId = null, NetBuffer? buffer = null)
        {
            List<ActionNodeBase>? candidates;
            if (triggerType == TriggerType.OnPacketReceived && packetId.HasValue)
            {
                packetTriggerNodes.TryGetValue(packetId.Value, out candidates);
            }
            else
            {
                triggerNodes.TryGetValue(triggerType, out candidates);
            }

            if (candidates == null || candidates.Count == 0)
            {
                return;
            }

            foreach (var node in candidates.Where(node => node.Trigger?.Matches(triggerType, packetId, buffer) == true))
            {
                Log.Debug("Triggering node: {NodeName}", node.Name);
                ExecuteNodeChain(client, node, buffer);
            }
        }

        private static void ExecuteNodeChain(Client client, ActionNodeBase node, NetBuffer? buffer)
        {
            node.Execute(client, buffer);

            foreach (var nextNode in node.NextNodes)
            {
                ExecuteNodeChain(client, nextNode, buffer);
            }
        }
    }
}
