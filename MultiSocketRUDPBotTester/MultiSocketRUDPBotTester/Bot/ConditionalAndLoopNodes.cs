using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.Contents.Client;
using Serilog;

namespace MultiSocketRUDPBotTester.Bot
{
    public abstract class ContextNodeBase : ActionNodeBase
    {
        public sealed override void Execute(Client client, NetBuffer? receivedPacket = null)
        {
            client.GlobalContext.SetPacket(receivedPacket);
            ExecuteImpl(client.GlobalContext);
        }

        protected abstract void ExecuteImpl(RuntimeContext context);
    }

    public class ConditionalNode : ContextNodeBase
    {
        public Func<RuntimeContext, bool>? Condition { get; set; }
        public List<ActionNodeBase> TrueNodes { get; set; } = [];
        public List<ActionNodeBase> FalseNodes { get; set; } = [];

        protected override void ExecuteImpl(RuntimeContext context)
        {
            if (Condition == null)
            {
                Log.Warning("ConditionalNode has no condition");
                return;
            }

            try
            {
                var result = Condition(context);
                Log.Debug("Conditional check: {Result}", result);

                var branchVisited = new HashSet<ActionNodeBase>();
                var nodesToExecute = result ? TrueNodes : FalseNodes;

                foreach (var node in nodesToExecute)
                {
                    ExecuteNodeChain(context, node, branchVisited);
                }

                Log.Debug("ConditionalNode: Executing {Count} next nodes after branch", NextNodes.Count);
                var nextVisited = new HashSet<ActionNodeBase>();

                foreach (var nextNode in NextNodes)
                {
                    ExecuteNodeChain(context, nextNode, nextVisited);
                }
            }
            catch (Exception ex)
            {
                Log.Error(ex, "Conditional evaluation failed");
                throw;
            }
        }

        private static void ExecuteNodeChain(RuntimeContext context, ActionNodeBase node, HashSet<ActionNodeBase> visited)
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
                ExecuteNodeChain(context, next, visited);
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

    public class LoopNode : ContextNodeBase
    {
        public Func<RuntimeContext, bool>? ContinueCondition { get; set; }
        public int MaxIterations { get; set; } = 100;
        public List<ActionNodeBase> LoopBody { get; set; } = [];
        public List<ActionNodeBase> ExitNodes { get; set; } = [];

        protected override void ExecuteImpl(RuntimeContext context)
        {
            if (ContinueCondition == null)
            {
                Log.Warning("LoopNode has no continue condition");
                return;
            }

            try
            {
                var loopInstanceKey = $"__loop_instance_{Guid.NewGuid()}";
                context.Set(loopInstanceKey, 0);

                var iteration = 0;
                while (iteration < MaxIterations && ContinueCondition(context))
                {
                    Log.Debug("Loop iteration: {Iteration}", iteration);
                    context.Set(loopInstanceKey, iteration);

                    var visited = new HashSet<ActionNodeBase>();
                    foreach (var node in LoopBody)
                    {
                        ExecuteNodeChain(context, node, visited);
                    }

                    iteration++;
                }

                if (iteration >= MaxIterations)
                {
                    Log.Warning("Loop reached maximum iterations: {Max}", MaxIterations);
                }

                var exitVisited = new HashSet<ActionNodeBase>();
                foreach (var node in ExitNodes)
                {
                    ExecuteNodeChain(context, node, exitVisited);
                }

                Log.Debug("LoopNode: Executing {Count} next nodes after loop", NextNodes.Count);
                var nextVisited = new HashSet<ActionNodeBase>();
                foreach (var nextNode in NextNodes)
                {
                    ExecuteNodeChain(context, nextNode, nextVisited);
                }
            }
            catch (Exception ex)
            {
                Log.Error("Loop execution failed: {Message}", ex.Message);
            }
        }

        private static void ExecuteNodeChain(RuntimeContext context, ActionNodeBase node, HashSet<ActionNodeBase> visited)
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
                ExecuteNodeChain(context, next, visited);
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

    public class RepeatTimerNode : ContextNodeBase
    {
        public int IntervalMilliseconds { get; set; } = 1000;
        public int RepeatCount { get; set; } = 10;
        public List<ActionNodeBase> RepeatBody { get; set; } = [];

        protected override void ExecuteImpl(RuntimeContext context)
        {
            var instanceId = Guid.NewGuid().ToString();
            var iterationKey = $"__repeat_iteration_{instanceId}";

            Task.Run(async () =>
            {
                try
                {
                    for (var i = 0; i < RepeatCount; i++)
                    {
                        Log.Debug("Repeat iteration: {Iteration}/{Total}", i + 1, RepeatCount);
                        context.Set(iterationKey, i);

                        var visited = new HashSet<ActionNodeBase>();
                        foreach (var node in RepeatBody)
                        {
                            ExecuteNodeChain(context, node, visited);
                        }

                        if (i < RepeatCount - 1)
                        {
                            await Task.Delay(IntervalMilliseconds);
                        }
                    }

                    var visitedNext = new HashSet<ActionNodeBase>();
                    foreach (var next in NextNodes)
                    {
                        ExecuteNodeChain(context, next, visitedNext);
                    }
                }
                catch (Exception e)
                {
                    Log.Error("RepeatTimerNode failed: {Message}", e.Message);
                }
            });
        }

        private static void ExecuteNodeChain(RuntimeContext context, ActionNodeBase node, HashSet<ActionNodeBase> visited)
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
                ExecuteNodeChain(context, next, visited);
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