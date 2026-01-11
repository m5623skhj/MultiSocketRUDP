using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.Contents.Client;
using Serilog;

namespace MultiSocketRUDPBotTester.Bot
{
    public abstract class ContextNodeBase : ActionNodeBase
    {
        public sealed override void Execute(Client client, NetBuffer? receivedPacket = null)
        {
            var context = new RuntimeContext(client, receivedPacket);
            ExecuteImpl(context);
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
                foreach (var node in nodesToExecute)
                {
                    ExecuteNodeChain(context, node);
                }
            }
            catch (Exception ex)
            {
                Log.Error(ex, "Conditional evaluation failed");
                throw;
            }
        }

        private static void ExecuteNodeChain(RuntimeContext context, ActionNodeBase node)
        {
            node.Execute(context.Client, context.Packet);

            foreach (var next in node.NextNodes)
            {
                ExecuteNodeChain(context, next);
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

            try
            {
                var iteration = 0;
                while (iteration < MaxIterations && ContinueCondition(context))
                {
                    Log.Debug("Loop iteration: {Iteration}", iteration);
                    foreach (var node in LoopBody)
                    {
                        ExecuteNodeChain(context, node);
                    }

                    iteration++;
                }

                if (iteration >= MaxIterations)
                {
                    Log.Warning("Loop reached maximum iterations: {Max}", MaxIterations);
                }

                foreach (var node in ExitNodes)
                {
                    ExecuteNodeChain(context, node);
                }
            }
            catch (Exception ex)
            {
                Log.Error("Loop execution failed: {Message}", ex.Message);
            }
        }

        private static void ExecuteNodeChain(RuntimeContext context, ActionNodeBase node)
        {
            node.Execute(context.Client, context.Packet);

            foreach (var next in node.NextNodes)
            {
                ExecuteNodeChain(context, next);
            }
        }
    }

    public class RepeatTimerNode : ContextNodeBase
    {
        public int IntervalMilliseconds { get; set; } = 1000;
        public int RepeatCount { get; set; } = 10;
        public List<ActionNodeBase> RepeatBody { get; set; } = [];

        protected override void ExecuteImpl(RuntimeContext context)
        {
            Task.Run(async () =>
            {
                try
                {
                    for (var i = 0; i < RepeatCount; i++)
                    {
                        Log.Debug("Repeat iteration: {Iteration}/{Total}", i + 1, RepeatCount);
                        foreach (var node in RepeatBody)
                        {
                            ExecuteNodeChain(context, node);
                        }

                        if (i < RepeatCount - 1)
                        {
                            await Task.Delay(IntervalMilliseconds);
                        }
                    }

                    foreach (var next in NextNodes)
                    {
                        ExecuteNodeChain(context, next);
                    }
                }
                catch (Exception e)
                {
                    Log.Error("RepeatTimerNode failed: {Message}", e.Message);
                }
            });
        }

        private static void ExecuteNodeChain(RuntimeContext context, ActionNodeBase node)
        {
            node.Execute(context.Client, context.Packet);

            foreach (var next in node.NextNodes)
            {
                ExecuteNodeChain(context, next);
            }
        }
    }
}
