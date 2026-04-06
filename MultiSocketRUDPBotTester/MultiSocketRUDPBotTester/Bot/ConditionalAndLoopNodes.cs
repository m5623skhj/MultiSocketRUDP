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

                var nodesToExecute = result ? TrueNodes : FalseNodes;
                var branchVisited = new HashSet<ActionNodeBase>();
                foreach (var node in nodesToExecute)
                {
                    NodeExecutionHelper.ExecuteChain(context, node, branchVisited);
                }

                Log.Debug("ConditionalNode: Executing {Count} next nodes after branch", NextNodes.Count);
                var nextVisited = new HashSet<ActionNodeBase>();
                foreach (var nextNode in NextNodes)
                {
                    NodeExecutionHelper.ExecuteChain(context, nextNode, nextVisited);
                }
            }
            catch (Exception ex)
            {
                Log.Error(ex, "Conditional evaluation failed");
                throw;
            }
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

            var cancellationToken = context.Client.CancellationToken.Token;
            Task.Run(async () =>
            {
                try
                {
                    var iteration = 0;
                    while (iteration < MaxIterations && ContinueCondition(context))
                    {
                        Log.Debug("Loop iteration: {Iteration}", iteration);
                        context.Set("__loop_iteration", iteration);

                        await context.GetAndClearPendingAsyncTask();

                        var visited = new HashSet<ActionNodeBase>();
                        foreach (var node in LoopBody)
                        {
                            NodeExecutionHelper.ExecuteChain(context, node, visited);
                        }

                        var pendingTask = context.GetAndClearPendingAsyncTask();
                        try
                        {
                            await pendingTask.WaitAsync(
                                TimeSpan.FromMilliseconds(TimeoutMilliseconds()),
                                cancellationToken).ConfigureAwait(false);
                        }
                        catch (TimeoutException)
                        {
                            Log.Warning("LoopNode: Body async task timed out at iteration {Iteration}, continuing",
                                iteration);
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
                        NodeExecutionHelper.ExecuteChain(context, node, exitVisited);
                    }

                    Log.Debug("LoopNode: Executing {Count} next nodes after loop", NextNodes.Count);
                    var nextVisited = new HashSet<ActionNodeBase>();
                    foreach (var nextNode in NextNodes)
                    {
                        NodeExecutionHelper.ExecuteChain(context, nextNode, nextVisited);
                    }
                }
                catch (OperationCanceledException)
                {
                    Log.Information("LoopNode: Cancelled");
                }
                catch (Exception ex)
                {
                    Log.Error("Loop execution failed: {Message}", ex.Message);
                    throw;
                }
            }, cancellationToken);
        }

        private static int TimeoutMilliseconds() => 60_000;
    }

    public class RepeatTimerNode : ContextNodeBase
    {
        public int IntervalMilliseconds { get; set; } = 1000;
        public int RepeatCount { get; set; } = 10;
        public List<ActionNodeBase> RepeatBody { get; set; } = [];

        protected override void ExecuteImpl(RuntimeContext context)
        {
            var iterationKey = $"__repeat_iteration_{Guid.NewGuid()}";
            var cancellationToken = context.Client.CancellationToken.Token;

            Task.Run(async () =>
            {
                try
                {
                    for (var i = 0; i < RepeatCount; i++)
                    {
                        if (cancellationToken.IsCancellationRequested)
                        {
                            Log.Information("RepeatTimerNode: Cancelled at iteration {I}", i);
                            return;
                        }

                        Log.Debug("Repeat iteration: {I}/{Total}", i + 1, RepeatCount);
                        context.Set(iterationKey, i);

                        var visited = new HashSet<ActionNodeBase>();
                        foreach (var node in RepeatBody)
                        {
                            NodeExecutionHelper.ExecuteChain(context, node, visited);
                        }

                        if (i < RepeatCount - 1)
                        {
                            await Task.Delay(IntervalMilliseconds, cancellationToken);
                        }
                    }

                    var nextVisited = new HashSet<ActionNodeBase>();
                    foreach (var next in NextNodes)
                    {
                        NodeExecutionHelper.ExecuteChain(context, next, nextVisited);
                    }
                }
                catch (OperationCanceledException)
                {
                    Log.Information("RepeatTimerNode: Cancelled");
                }
                catch (Exception e)
                {
                    Log.Error("RepeatTimerNode failed: {Message}", e.Message);
                }
            }, cancellationToken);
        }
    }
}
