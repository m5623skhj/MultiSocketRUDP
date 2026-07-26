using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.Contents.Client;
using Serilog;

namespace MultiSocketRUDPBotTester.Bot
{
    internal static class NodeExecutionHelper
    {
        private static readonly AsyncLocal<HashSet<ActionNodeBase>?> activeExecutionPath = new();

        public static bool HandlesOwnNextNode(ActionNodeBase node) =>
            node is DelayNode
                or RandomDelayNode
                or RandomChoiceNode
                or RepeatTimerNode
                or WaitForPacketNode
                or RetryNode
                or ConditionalNode
                or LoopNode
                or AssertNode;

        public static void ExecuteChain(
            RuntimeContext context,
            ActionNodeBase node,
            HashSet<ActionNodeBase> visited)
        {
            ExecuteChainWithStats(context, node, context.GetPacket(), visited);
        }

        internal static void ExecuteChain(
            RuntimeContext context,
            ActionNodeBase node,
            NetBuffer? buffer,
            HashSet<ActionNodeBase> visited)
        {
            ExecuteChainWithStats(context, node, buffer, visited);
        }

        private static void ExecuteChainWithStats(
            RuntimeContext context,
            ActionNodeBase node,
            NetBuffer? buffer,
            HashSet<ActionNodeBase> visited)
        {
            var inheritedPath = activeExecutionPath.Value;
            if (inheritedPath?.Contains(node) == true || !visited.Add(node))
            {
                Log.Warning("Circular reference detected in node: {NodeName}", node.Name);
                return;
            }

            var currentPath = inheritedPath == null
                ? new HashSet<ActionNodeBase>()
                : new HashSet<ActionNodeBase>(inheritedPath);
            currentPath.Add(node);
            activeExecutionPath.Value = currentPath;

            Log.Debug("Executing node: {NodeName}", node.Name);

            var sw = System.Diagnostics.Stopwatch.StartNew();
            var success = true;
            string? error = null;

            try
            {
                if (node is ContextNodeBase contextNode)
                {
                    contextNode.ExecuteWithContext(context, buffer);
                }
                else
                {
                    node.Execute(context.Client, buffer);
                }

                if (HandlesOwnNextNode(node))
                {
                    Log.Debug("Node {NodeName} handles its own NextNodes", node.Name);
                    return;
                }

                foreach (var next in node.NextNodes)
                {
                    ExecuteChainWithStats(context, next, buffer, visited);
                }
            }
            catch (Exception ex)
            {
                success = false;
                error = ex.Message;
                Log.Error("Node execution failed: {NodeName} - {Error}", node.Name, ex.Message);
                throw;
            }
            finally
            {
                activeExecutionPath.Value = inheritedPath;
                sw.Stop();
                ActionNodeBase.GetStatsTracker()?.RecordExecution(node.Name, sw.ElapsedMilliseconds, success, error);
                context.IncrementExecutionCount(node.Name);
                context.RecordMetric($"{node.Name}_time", sw.ElapsedMilliseconds);
            }
        }

        public static void ExecuteChainWithStats(
            Client client,
            ActionNodeBase node,
            NetBuffer? buffer,
            HashSet<ActionNodeBase> visited)
        {
            ExecuteChainWithStats(client.GlobalContext, node, buffer, visited);
        }
    }
}
