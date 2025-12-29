using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.Contents.Client;

namespace MultiSocketRUDPBotTester.Bot
{
    public class ActionGraphBuilder
    {
        private readonly ActionGraph graph = new();
        private ActionNodeBase? lastNode;

        public ActionGraph Build() => graph;

        public ActionGraphBuilder WithName(string name)
        {
            graph.Name = name;
            return this;
        }

        public ActionGraphBuilder OnConnected(string nodeName)
        {
            lastNode = new CustomActionNode
            {
                Name = nodeName,
                Trigger = new TriggerCondition { Type = TriggerType.OnConnected }
            };
            graph.AddNode(lastNode);
            return this;
        }

        public ActionGraphBuilder OnDisconnected(string nodeName)
        {
            lastNode = new CustomActionNode
            {
                Name = nodeName,
                Trigger = new TriggerCondition { Type = TriggerType.OnDisconnected }
            };
            graph.AddNode(lastNode);
            return this;
        }

        public ActionGraphBuilder OnReceive(string nodeName, PacketId packetId, Func<NetBuffer, bool>? validator = null)
        {
            lastNode = new CustomActionNode
            {
                Name = nodeName,
                Trigger = new TriggerCondition
                {
                    Type = TriggerType.OnPacketReceived,
                    PacketId = packetId,
                    PacketValidator = validator
                }
            };
            graph.AddNode(lastNode);
            return this;
        }

        public ActionGraphBuilder ThenSend(string nodeName, PacketId packetId, Func<Client, NetBuffer> packetBuilder)
        {
            var node = new SendPacketNode
            {
                Name = nodeName,
                PacketId = packetId,
                PacketBuilder = packetBuilder
            };

            lastNode?.NextNodes.Add(node);
            graph.AddNode(node);
            lastNode = node;
            return this;
        }

        public ActionGraphBuilder ThenDo(string nodeName, Action<Client, NetBuffer?> action)
        {
            var node = new CustomActionNode
            {
                Name = nodeName,
                ActionHandler = action
            };

            lastNode?.NextNodes.Add(node);
            graph.AddNode(node);
            lastNode = node;
            return this;
        }

        public ActionGraphBuilder ThenLog(string nodeName, Func<Client, NetBuffer?, string> messageBuilder)
        {
            var node = new LogNode
            {
                Name = nodeName,
                MessageBuilder = messageBuilder
            };

            lastNode?.NextNodes.Add(node);
            graph.AddNode(node);
            lastNode = node;
            return this;
        }

        public ActionGraphBuilder ThenWait(string nodeName, int milliseconds)
        {
            var node = new DelayNode
            {
                Name = nodeName,
                DelayMilliseconds = milliseconds
            };

            lastNode?.NextNodes.Add(node);
            graph.AddNode(node);
            lastNode = node;
            return this;
        }

        public ConditionalBranchBuilder ThenIf(string nodeName, Func<RuntimeContext, bool> condition)
        {
            var node = new ConditionalNode
            {
                Name = nodeName,
                Condition = condition
            };

            lastNode?.NextNodes.Add(node);
            graph.AddNode(node);
            lastNode = node;

            return new ConditionalBranchBuilder(this, node);
        }

        public LoopBranchBuilder ThenLoop(string nodeName, Func<RuntimeContext, bool> continueCondition, int maxIterations = 100)
        {
            var node = new LoopNode
            {
                Name = nodeName,
                ContinueCondition = continueCondition,
                MaxIterations = maxIterations
            };

            lastNode?.NextNodes.Add(node);
            graph.AddNode(node);
            lastNode = node;

            return new LoopBranchBuilder(this, node);
        }

        public RepeatBranchBuilder ThenRepeat(string nodeName, int count, int intervalMs = 1000)
        {
            var node = new RepeatTimerNode
            {
                Name = nodeName,
                RepeatCount = count,
                IntervalMilliseconds = intervalMs
            };

            lastNode?.NextNodes.Add(node);
            graph.AddNode(node);
            lastNode = node;

            return new RepeatBranchBuilder(this, node);
        }

        internal void SetLastNode(ActionNodeBase node)
        {
            lastNode = node;
        }
    }

    public class ConditionalBranchBuilder(ActionGraphBuilder parent, ConditionalNode conditionalNode)
    {
        private ActionNodeBase? lastTrueNode;
        private ActionNodeBase? lastFalseNode;

        public ConditionalBranchBuilder OnTrue(Action<ConditionalBranchBuilder> trueBuilder)
        {
            trueBuilder(this);
            return this;
        }

        public ConditionalBranchBuilder OnFalse(Action<ConditionalBranchBuilder> falseBuilder)
        {
            falseBuilder(this);
            return this;
        }

        public ConditionalBranchBuilder TrueDo(string nodeName, Action<Client, NetBuffer?> action)
        {
            var node = new CustomActionNode
            {
                Name = nodeName,
                ActionHandler = action
            };

            conditionalNode.TrueNodes.Add(node);
            lastTrueNode = node;
            return this;
        }

        public ConditionalBranchBuilder FalseDo(string nodeName, Action<Client, NetBuffer?> action)
        {
            var node = new CustomActionNode
            {
                Name = nodeName,
                ActionHandler = action
            };

            conditionalNode.FalseNodes.Add(node);
            lastFalseNode = node;
            return this;
        }

        public ActionGraphBuilder EndIf()
        {
            if (lastTrueNode != null)
            {
                parent.SetLastNode(lastTrueNode);
            }
            else if (lastFalseNode != null)
            {
                parent.SetLastNode(lastFalseNode);
            }

            return parent;
        }
    }

    public class LoopBranchBuilder(ActionGraphBuilder parent, LoopNode loopNode)
    {
        public LoopBranchBuilder Do(string nodeName, Action<Client, NetBuffer?> action)
        {
            var node = new CustomActionNode
            {
                Name = nodeName,
                ActionHandler = action
            };

            loopNode.LoopBody.Add(node);
            return this;
        }

        public LoopBranchBuilder OnExit(string nodeName, Action<Client, NetBuffer?> action)
        {
            var node = new CustomActionNode
            {
                Name = nodeName,
                ActionHandler = action
            };

            loopNode.ExitNodes.Add(node);
            return this;
        }

        public ActionGraphBuilder EndLoop()
        {
            parent.SetLastNode(loopNode);
            return parent;
        }
    }

    public class RepeatBranchBuilder(ActionGraphBuilder parent, RepeatTimerNode repeatNode)
    {
        public RepeatBranchBuilder Do(string nodeName, Action<Client, NetBuffer?> action)
        {
            var node = new CustomActionNode
            {
                Name = nodeName,
                ActionHandler = action
            };

            repeatNode.RepeatBody.Add(node);
            return this;
        }

        public ActionGraphBuilder EndRepeat()
        {
            parent.SetLastNode(repeatNode);
            return parent;
        }
    }
}