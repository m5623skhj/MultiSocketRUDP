using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.Contents.Client;
using Serilog;
using System.Collections.Concurrent;

namespace MultiSocketRUDPBotTester.Bot
{
    public class ActionGraph
    {
        private readonly ConcurrentDictionary<TriggerType, List<ActionNodeBase>> triggerNodes = new();
        private readonly ConcurrentDictionary<PacketId, List<ActionNodeBase>> packetTriggerNodes = new();
        private readonly List<ActionNodeBase> allNodes = [];
        private readonly Lock allNodesLock = new();
        private readonly Lock triggerNodesLock = new();
        private readonly Lock packetTriggerNodesLock = new();

        public string Name { get; set; } = "Unnamed Graph";

        public List<ActionNodeBase> GetAllNodes()
        {
            lock (allNodesLock)
            {
                return allNodes.ToList();
            }
        }

        public void AddNode(ActionNodeBase node)
        {
            lock (allNodesLock)
            {
                allNodes.Add(node);
            }

            if (node.Trigger == null)
            {
                return;
            }

            lock (triggerNodesLock)
            {
                if (!triggerNodes.TryGetValue(node.Trigger.Type, out var list))
                {
                    list = [];
                    triggerNodes[node.Trigger.Type] = list;
                }
                list.Add(node);
            }

            if (node.Trigger.Type != TriggerType.OnPacketReceived || !node.Trigger.PacketId.HasValue)
            {
                return;
            }

            var packetId = node.Trigger.PacketId.Value;
            lock (packetTriggerNodesLock)
            {
                if (!packetTriggerNodes.TryGetValue(packetId, out var list))
                {
                    list = [];
                    packetTriggerNodes[packetId] = list;
                }
                list.Add(node);
            }
        }

        public void TriggerEvent(Client client, TriggerType triggerType, PacketId? packetId = null, NetBuffer? buffer = null)
        {
            Log.Debug("TriggerEvent called - Type: {Type}, PacketId: {PacketId}", triggerType, packetId);

            var candidates = ResolveCandidates(triggerType, packetId);
            if (candidates == null || candidates.Count == 0)
            {
                Log.Debug("No matching nodes found for trigger");
                return;
            }

            foreach (var node in candidates.Where(n => n.Trigger?.Matches(triggerType, packetId, buffer) == true))
            {
                Log.Information("Triggering node: {NodeName} (Type: {TriggerType})", node.Name, triggerType);
                try
                {
                    var visited = new HashSet<ActionNodeBase>();
                    NodeExecutionHelper.ExecuteChainWithStats(client, node, buffer, visited);
                    Log.Information("Node executed successfully: {NodeName}", node.Name);
                }
                catch (Exception ex)
                {
                    Log.Error("Node execution failed: {NodeName} - {Error}", node.Name, ex.Message);
                }
            }
        }

        private List<ActionNodeBase>? ResolveCandidates(TriggerType triggerType, PacketId? packetId)
        {
            if (triggerType == TriggerType.OnPacketReceived && packetId.HasValue)
            {
                lock (packetTriggerNodesLock)
                {
                    if (!packetTriggerNodes.TryGetValue(packetId.Value, out var list))
                    {
                        return null;
                    }

                    Log.Debug("Found {Count} nodes for PacketId {PacketId}", list.Count, packetId);
                    return list.ToList();
                }
            }

            lock (triggerNodesLock)
            {
                if (!triggerNodes.TryGetValue(triggerType, out var list))
                {
                    return null;
                }

                Log.Debug("Found {Count} nodes for TriggerType {Type}", list.Count, triggerType);
                return list.ToList();
            }
        }
    }
}