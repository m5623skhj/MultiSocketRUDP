using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.Contents.Client;
using Serilog;

namespace MultiSocketRUDPBotTester.Bot
{
    public class ConditionalNode : ActionNodeBase
    {
        public Func<Client, NetBuffer?, bool>? Condition { get; set; }
        public List<ActionNodeBase> TrueNodes { get; set; } = [];
        public List<ActionNodeBase> FalseNodes { get; set; } = [];

        public override void Execute(Client client, NetBuffer? receivedPacket = null)
        {
            if (Condition == null)
            {
                Log.Warning("ConditionalNode has no condition");
                return;
            }

            try
            {
                var result = Condition(client, receivedPacket);
                Log.Debug("Conditional check: {Result}", result);

                var nodesToExecute = result ? TrueNodes : FalseNodes;
                foreach (var node in nodesToExecute)
                {
                    ExecuteNodeChain(client, node, receivedPacket);
                }
            }
            catch (Exception ex)
            {
                Log.Error("Conditional evaluation failed: {Message}", ex.Message);
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

    public class LoopNode : ActionNodeBase
    {
        public Func<Client, NetBuffer?, bool>? ContinueCondition { get; set; }
        public int MaxIterations { get; set; } = 100;
        public List<ActionNodeBase> LoopBody { get; set; } = [];
        public List<ActionNodeBase> ExitNodes { get; set; } = [];

        public override void Execute(Client client, NetBuffer? receivedPacket = null)
        {
            if (ContinueCondition == null)
            {
                Log.Warning("LoopNode has no continue condition");
                return;
            }

            try
            {
                var iteration = 0;
                while (iteration < MaxIterations && ContinueCondition(client, receivedPacket))
                {
                    Log.Debug("Loop iteration: {Iteration}", iteration);

                    foreach (var node in LoopBody)
                    {
                        ExecuteNodeChain(client, node, receivedPacket);
                    }

                    iteration++;
                }

                if (iteration >= MaxIterations)
                {
                    Log.Warning("Loop reached maximum iterations: {Max}", MaxIterations);
                }

                foreach (var node in ExitNodes)
                {
                    ExecuteNodeChain(client, node, receivedPacket);
                }
            }
            catch (Exception ex)
            {
                Log.Error("Loop execution failed: {Message}", ex.Message);
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
    public class RepeatTimerNode : ActionNodeBase
    {
        public int IntervalMilliseconds { get; set; } = 1000;
        public int RepeatCount { get; set; } = 10;
        public List<ActionNodeBase> RepeatBody { get; set; } = [];

        public override void Execute(Client client, NetBuffer? receivedPacket = null)
        {
            Task.Run(async () =>
            {
                for (var i = 0; i < RepeatCount; i++)
                {
                    Log.Debug("Repeat iteration: {Iteration}/{Total}", i + 1, RepeatCount);

                    foreach (var node in RepeatBody)
                    {
                        ExecuteNodeChain(client, node, receivedPacket);
                    }

                    if (i < RepeatCount - 1)
                    {
                        await Task.Delay(IntervalMilliseconds);
                    }
                }

                foreach (var nextNode in NextNodes)
                {
                    ExecuteNodeChain(client, nextNode, receivedPacket);
                }
            });
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