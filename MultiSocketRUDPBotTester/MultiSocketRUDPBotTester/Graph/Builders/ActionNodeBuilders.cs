using MultiSocketRUDPBotTester.Bot;
using Serilog;

namespace MultiSocketRUDPBotTester.Graph.Builders
{
    public class SendPacketNodeBuilder : INodeBuilder
    {
        public bool CanBuild(NodeVisual visual) => visual.NodeType == typeof(SendPacketNode);

        public ActionNodeBase Build(NodeVisual visual)
        {
            var packetId = visual.Configuration?.PacketId ?? PacketId.InvalidPacketId;
            if (packetId == PacketId.InvalidPacketId)
            {
                throw new InvalidOperationException(
                    $"SendPacketNode '{visual.NodeType!.Name}' requires a valid PacketId. " +
                    "Please double-click the node to configure it.");
            }

            var fieldValues = new Dictionary<string, object>();
            var schema = PacketSchema.Get(packetId);
            if (schema != null)
            {
                foreach (var field in schema)
                {
                    var key = $"Field_{field.Name}";
                    if (visual.Configuration?.Properties.TryGetValue(key, out var val) == true && val != null)
                    {
                        fieldValues[field.Name] = field.Type switch
                        {
                            FieldType.Byte => byte.Parse(val.ToString()!),
                            FieldType.Ushort => ushort.Parse(val.ToString()!),
                            FieldType.Int => int.Parse(val.ToString()!),
                            FieldType.Uint => uint.Parse(val.ToString()!),
                            FieldType.Ulong => ulong.Parse(val.ToString()!),
                            FieldType.String => val.ToString()!,
                            _ => val
                        };
                    }
                }
            }

            return new SendPacketNode
            {
                Name = visual.NodeType!.Name,
                PacketId = packetId,
                FieldValues = fieldValues
            };
        }
    }

    public class DelayNodeBuilder : INodeBuilder
    {
        public bool CanBuild(NodeVisual visual) => visual.NodeType == typeof(DelayNode);

        public ActionNodeBase Build(NodeVisual visual) => new DelayNode
        {
            Name = visual.NodeType!.Name,
            DelayMilliseconds = visual.Configuration?.IntValue ?? 1000
        };
    }

    public class RandomDelayNodeBuilder : INodeBuilder
    {
        public bool CanBuild(NodeVisual visual) => visual.NodeType == typeof(RandomDelayNode);

        public ActionNodeBase Build(NodeVisual visual) => new RandomDelayNode
        {
            Name = visual.NodeType!.Name,
            MinDelayMilliseconds = visual.Configuration?.Properties.GetValueOrDefault("MinDelay") as int? ?? 500,
            MaxDelayMilliseconds = visual.Configuration?.Properties.GetValueOrDefault("MaxDelay") as int? ?? 2000
        };
    }

    public class DisconnectNodeBuilder : INodeBuilder
    {
        public bool CanBuild(NodeVisual visual) => visual.NodeType == typeof(DisconnectNode);

        public ActionNodeBase Build(NodeVisual visual) => new DisconnectNode
        {
            Name = visual.NodeType!.Name,
            Reason = visual.Configuration?.StringValue ?? "User requested disconnect"
        };
    }

    public class LogNodeBuilder : INodeBuilder
    {
        public bool CanBuild(NodeVisual visual) => visual.NodeType == typeof(LogNode);

        public ActionNodeBase Build(NodeVisual visual)
        {
            var logMessage = visual.Configuration?.StringValue ?? "No log message configured";
            return new LogNode
            {
                Name = visual.NodeType!.Name,
                MessageBuilder = (client, buffer) =>
                {
                    var msg = logMessage
                        .Replace("{sessionId}", client.GetSessionId().ToString())
                        .Replace("{isConnected}", client.IsConnected().ToString());

                    if (buffer != null)
                    {
                        msg = msg.Replace("{packetSize}", buffer.GetLength().ToString());
                    }

                    return msg;
                }
            };
        }
    }

    public class CustomActionNodeBuilder : INodeBuilder
    {
        public bool CanBuild(NodeVisual visual) => visual.NodeType == typeof(CustomActionNode);

        public ActionNodeBase Build(NodeVisual visual) => new CustomActionNode
        {
            Name = visual.NodeType!.Name,
            ActionHandler = (_, _) => Log.Information("Custom action: {Name}", visual.NodeType.Name)
        };
    }
}
