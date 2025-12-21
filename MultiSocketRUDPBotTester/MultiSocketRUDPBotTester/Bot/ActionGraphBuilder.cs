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
    }
}
