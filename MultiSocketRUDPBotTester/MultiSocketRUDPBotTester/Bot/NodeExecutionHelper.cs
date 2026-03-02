using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.Contents.Client;
using Serilog;

namespace MultiSocketRUDPBotTester.Bot
{
    internal static class NodeExecutionHelper
    {
        public static bool IsAsyncNode(ActionNodeBase node) =>
            node is DelayNode
                or RandomDelayNode
                or RepeatTimerNode
                or WaitForPacketNode
                or RetryNode
                or ConditionalNode
                or LoopNode;

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

            if (IsAsyncNode(node))
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

            var sw = System.Diagnostics.Stopwatch.StartNew();
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
                sw.Stop();
                ActionNodeBase.GetStatsTracker()?.RecordExecution(node.Name, sw.ElapsedMilliseconds, success, error);
                client.GlobalContext.IncrementExecutionCount(node.Name);
                client.GlobalContext.RecordMetric($"{node.Name}_time", sw.ElapsedMilliseconds);
            }

            if (IsAsyncNode(node))
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