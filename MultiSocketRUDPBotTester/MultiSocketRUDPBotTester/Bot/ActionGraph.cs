using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.Contents.Client;
using Serilog;
using System.Collections.Concurrent;
using MultiSocketRUDPBotTester.ClientCore;

namespace MultiSocketRUDPBotTester.Bot
{
    public class ActionGraph
    {
        private readonly ConcurrentDictionary<TriggerType, List<ActionNodeBase>> triggerNodes = new();
        private readonly ConcurrentDictionary<PacketId, List<ActionNodeBase>> packetTriggerNodes = new();
        private readonly List<ActionNodeBase> allNodes = [];
        private readonly Lock allNodesLock = new();

        public string Name { get; set; } = "Unnamed Graph";

        public List<ActionNodeBase> GetAllNodes()
        {
            lock (allNodesLock)
            {
                return allNodes.ToList();
            }
        }

        private readonly Lock triggerNodesLock = new();
        private readonly Lock packetTriggerNodesLock = new();

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

            List<ActionNodeBase>? candidates = null;

            if (triggerType == TriggerType.OnPacketReceived && packetId.HasValue)
            {
                lock (packetTriggerNodesLock)
                {
                    if (packetTriggerNodes.TryGetValue(packetId.Value, out var list))
                    {
                        candidates = list.ToList();
                    }
                }
                Log.Debug("Found {Count} nodes for PacketId {PacketId}", candidates?.Count ?? 0, packetId);
            }
            else
            {
                lock (triggerNodesLock)
                {
                    if (triggerNodes.TryGetValue(triggerType, out var list))
                    {
                        candidates = list.ToList();
                    }
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
                    var visited = new HashSet<ActionNodeBase>();
                    ExecuteNodeChain(client, node, buffer, visited);
                    Log.Information("Node executed successfully: {NodeName}", node.Name);
                }
                catch (Exception ex)
                {
                    Log.Error("Node execution failed: {NodeName} - {Error}", node.Name, ex.Message);
                }
            }
        }

        private static void ExecuteNodeChain(Client client, ActionNodeBase node, NetBuffer? buffer, HashSet<ActionNodeBase> visited)
        {
            if (!visited.Add(node))
            {
                Log.Warning("Circular reference detected in execution chain: {NodeName}", node.Name);
                return;
            }

            Log.Debug("Executing node: {NodeName}", node.Name);
            node.Execute(client, buffer);

            if (IsAsyncNode(node))
            {
                Log.Debug("Node {NodeName} is async, it will handle its own NextNodes", node.Name);
                return;
            }

            if (node.NextNodes.Count > 0)
            {
                Log.Debug("Node {NodeName} has {Count} next nodes", node.Name, node.NextNodes.Count);
            }

            foreach (var nextNode in node.NextNodes)
            {
                ExecuteNodeChain(client, nextNode, buffer, visited);
            }
        }

        private static bool IsAsyncNode(ActionNodeBase node)
        {
            return node is DelayNode
                or RandomDelayNode
                or RepeatTimerNode
                or WaitForPacketNode
                or RetryNode
                or ConditionalNode
                or LoopNode;
        }
    }
}