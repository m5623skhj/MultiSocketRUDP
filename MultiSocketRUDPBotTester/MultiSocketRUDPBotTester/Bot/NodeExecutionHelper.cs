using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.Contents.Client;
using Serilog;

namespace MultiSocketRUDPBotTester.Bot
{
    internal static class NodeExecutionHelper
    {
        public static bool HandlesOwnNextNode(ActionNodeBase node) =>
            node is DelayNode
                or RandomDelayNode
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
            if (!visited.Add(node))
            {
                Log.Warning("Circular reference detected in node: {NodeName}", node.Name);
                return;
            }

            node.Execute(context.Client, context.GetPacket());

            if (HandlesOwnNextNode(node))
            {
                return;
            }

            foreach (var next in node.NextNodes)
            {
                ExecuteChain(context, next, visited);
            }
        }

        public static void ExecuteChainWithStats(
            Client client,
            ActionNodeBase node,
            NetBuffer? buffer,
            HashSet<ActionNodeBase> visited)
        {
            if (!visited.Add(node))
            {
                Log.Warning("Circular reference detected in execution chain: {NodeName}", node.Name);
                return;
            }

            Log.Debug("Executing node: {NodeName}", node.Name);

            var startTs = System.Diagnostics.Stopwatch.GetTimestamp();
            var success = true;
            string? error = null;

            try
            {
                node.Execute(client, buffer);
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
                var elapsedMs = (System.Diagnostics.Stopwatch.GetTimestamp() - startTs) * 1000L / System.Diagnostics.Stopwatch.Frequency;
                ActionNodeBase.GetStatsTracker()?.RecordExecution(node.Name, elapsedMs, success, error);
                client.GlobalContext.Increment(node.ExecCountKey);
                client.GlobalContext.RecordMetricRaw(node.MetricFullKey, elapsedMs);
            }

            if (HandlesOwnNextNode(node))
            {
                Log.Debug("Node {NodeName} is async, it will handle its own NextNodes", node.Name);
                return;
            }

            foreach (var nextNode in node.NextNodes)
            {
                ExecuteChainWithStats(client, nextNode, buffer, visited);
            }
        }
    }
}
