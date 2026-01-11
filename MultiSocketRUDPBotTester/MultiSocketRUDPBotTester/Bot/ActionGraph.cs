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

        public string Name { get; set; } = "Unnamed Graph";

        public List<ActionNodeBase> GetAllNodes()
        {
            lock (allNodes)
            {
                return [..allNodes];
            }
        }

        public void AddNode(ActionNodeBase node)
        {
            lock (allNodes)
            {
                allNodes.Add(node);
            }

            if (node.Trigger == null)
            {
                return;
            }

            lock (triggerNodes)
            {
                if (!triggerNodes.ContainsKey(node.Trigger.Type))
                {
                    triggerNodes[node.Trigger.Type] = [];
                }
                triggerNodes[node.Trigger.Type].Add(node);
            }

            if (node.Trigger.Type != TriggerType.OnPacketReceived || !node.Trigger.PacketId.HasValue)
            {
                return;
            }

            lock (packetTriggerNodes)
            {
                var packetId = node.Trigger.PacketId.Value;
                if (!packetTriggerNodes.ContainsKey(packetId))
                {
                    packetTriggerNodes[packetId] = [];
                }
                packetTriggerNodes[packetId].Add(node);
            }
        }

        public void TriggerEvent(Client client, TriggerType triggerType, PacketId? packetId = null, NetBuffer? buffer = null)
        {
            Log.Debug("TriggerEvent called - Type: {Type}, PacketId: {PacketId}", triggerType, packetId);

            List<ActionNodeBase>? candidates;
            if (triggerType == TriggerType.OnPacketReceived && packetId.HasValue)
            {
                lock (packetTriggerNodes)
                {
                    packetTriggerNodes.TryGetValue(packetId.Value, out candidates);
                }
                Log.Debug("Found {Count} nodes for PacketId {PacketId}", candidates?.Count ?? 0, packetId);
            }
            else
            {
                lock (triggerNodes)
                {
                    triggerNodes.TryGetValue(triggerType, out candidates);
                }

                Log.Debug("Found {Count} nodes for TriggerType {Type}", candidates?.Count ?? 0, triggerType);
            }

            if (candidates == null || candidates.Count == 0)
            {
                Log.Debug("No matching nodes found for trigger");
                return;
            }

            foreach (var node in candidates.Where(node => node.Trigger?.Matches(triggerType, packetId, buffer) == true))
            {
                Log.Information("Triggering node: {NodeName} (Type: {TriggerType})", node.Name, triggerType);
                try
                {
                    ExecuteNodeChain(client, node, buffer);
                    Log.Information("Node executed successfully: {NodeName}", node.Name);
                }
                catch (Exception ex)
                {
                    Log.Error("Node execution failed: {NodeName} - {Error}", node.Name, ex.Message);
                }
            }
        }

        private static void ExecuteNodeChain(Client client, ActionNodeBase node, NetBuffer? buffer)
        {
            Log.Debug("Executing node: {NodeName}", node.Name);
            node.Execute(client, buffer);

            if (node.NextNodes.Count > 0)
            {
                Log.Debug("Node {NodeName} has {Count} next nodes", node.Name, node.NextNodes.Count);
            }

            foreach (var nextNode in node.NextNodes)
            {
                ExecuteNodeChain(client, nextNode, buffer);
            }
        }
    }
}