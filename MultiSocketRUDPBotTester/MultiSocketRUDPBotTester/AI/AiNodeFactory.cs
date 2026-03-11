using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using MultiSocketRUDPBotTester.Bot;
using WpfCanvas = System.Windows.Controls.Canvas;

namespace MultiSocketRUDPBotTester.AI
{
    public class AiNodeFactory
    {
        private readonly WpfCanvas canvas;
        private readonly List<NodeVisual> allNodes;
        private readonly Func<string, Brush, Border> createNodeVisual;
        private readonly Func<FrameworkElement> createInputPort;
        private readonly Func<string, FrameworkElement> createOutputPort;
        private readonly Action<NodeVisual> addNodeToCanvas;
        private readonly Action<string> log;

        public AiNodeFactory(
            WpfCanvas canvas,
            List<NodeVisual> allNodes,
            Func<string, Brush, Border> createNodeVisual,
            Func<FrameworkElement> createInputPort,
            Func<string, FrameworkElement> createOutputPort,
            Action<NodeVisual> addNodeToCanvas,
            Action<string> log)
        {
            this.canvas = canvas;
            this.allNodes = allNodes;
            this.createNodeVisual = createNodeVisual;
            this.createInputPort = createInputPort;
            this.createOutputPort = createOutputPort;
            this.addNodeToCanvas = addNodeToCanvas;
            this.log = log;
        }

        public NodeVisual? CreateFromJson(
            JsonElement jsonNode,
            double x,
            double y,
            Dictionary<string, NodeVisual> createdNodes,
            string nodePath)
        {
            if (!jsonNode.TryGetProperty("type", out var typeProp))
            {
                log($"Warning: Node at '{nodePath}' has no type property");
                return null;
            }

            var nodeTypeName = typeProp.GetString();
            if (string.IsNullOrEmpty(nodeTypeName))
            {
                log($"Warning: Node at '{nodePath}' has empty type");
                return null;
            }

            var nodeType = Type.GetType($"MultiSocketRUDPBotTester.Bot.{nodeTypeName}");
            if (nodeType == null)
            {
                log($"Warning: Could not find type {nodeTypeName} at '{nodePath}'");
                return null;
            }

            var category = ResolveCategory(nodeTypeName);
            var border = createNodeVisual(nodeTypeName, GetNodeColor(category));

            var visual = new NodeVisual
            {
                Border = border,
                Category = category,
                InputPort = createInputPort(),
                NodeType = nodeType,
                Configuration = new NodeConfiguration()
            };

            if (category == BotActionGraphWindow.NodeCategory.Action)
                visual.OutputPort = createOutputPort("default");
            else
            {
                visual.OutputPortTrue = createOutputPort(
                    category == BotActionGraphWindow.NodeCategory.Condition ? "true" : "continue");
                visual.OutputPortFalse = createOutputPort(
                    category == BotActionGraphWindow.NodeCategory.Condition ? "false" : "exit");
            }

            Configure(visual, jsonNode);

            WpfCanvas.SetLeft(border, x);
            WpfCanvas.SetTop(border, y);
            addNodeToCanvas(visual);
            createdNodes[nodePath] = visual;

            var childX = x + 200;
            var childY = y;

            childY = TryCreateChild(jsonNode, "next", childX, childY, createdNodes, nodePath,
                child =>
                {
                    if (visual.Category != BotActionGraphWindow.NodeCategory.Action)
                        return;
                    visual.Next = child;
                    visual.NextPortType = "default";
                });

            childY = TryCreateChild(jsonNode, "true_branch", childX, childY, createdNodes, nodePath,
                child => { visual.TrueChild = child; visual.TruePortType = "true"; });

            childY = TryCreateChild(jsonNode, "false_branch", childX, childY, createdNodes, nodePath,
                child => { visual.FalseChild = child; visual.FalsePortType = "false"; });

            childY = TryCreateChild(jsonNode, "loop_body", childX, childY, createdNodes, nodePath,
                child => { visual.TrueChild = child; visual.TruePortType = "continue"; });

            childY = TryCreateChild(jsonNode, "exit_nodes", childX, childY, createdNodes, nodePath,
                child => { visual.FalseChild = child; visual.FalsePortType = "exit"; });

            childY = TryCreateChild(jsonNode, "repeat_body", childX, childY, createdNodes, nodePath,
                child => { visual.TrueChild = child; visual.TruePortType = "continue"; });

            childY = TryCreateChild(jsonNode, "retry_body", childX, childY, createdNodes, nodePath,
                child => { visual.TrueChild = child; visual.TruePortType = "continue"; });

            childY = TryCreateChild(jsonNode, "timeout_nodes", childX, childY, createdNodes, nodePath,
                child => { visual.FalseChild = child; visual.FalsePortType = "exit"; });

            TryCreateChild(jsonNode, "failure_nodes", childX, childY, createdNodes, nodePath,
                child => { visual.FalseChild = child; visual.FalsePortType = "exit"; });

            return visual;
        }

        private double TryCreateChild(
            JsonElement jsonNode,
            string propertyName,
            double childX,
            double childY,
            Dictionary<string, NodeVisual> createdNodes,
            string nodePath,
            Action<NodeVisual> onCreated)
        {
            if (!jsonNode.TryGetProperty(propertyName, out var childJson))
                return childY;

            var child = CreateFromJson(
                childJson, childX, childY, createdNodes, $"{nodePath}.{propertyName}");

            if (child == null)
                return childY;

            onCreated(child);
            return childY + 150;
        }

        private void Configure(NodeVisual visual, JsonElement jsonNode)
        {
            if (visual.Configuration == null) return;

            try
            {
                if (visual.NodeType == typeof(SendPacketNode)) ConfigureSendPacket(visual, jsonNode);
                else if (visual.NodeType == typeof(DelayNode)) ConfigureDelay(visual, jsonNode);
                else if (visual.NodeType == typeof(RandomDelayNode)) ConfigureRandomDelay(visual, jsonNode);
                else if (visual.NodeType == typeof(LogNode)) ConfigureLog(visual, jsonNode);
                else if (visual.NodeType == typeof(DisconnectNode)) ConfigureDisconnect(visual, jsonNode);
                else if (visual.NodeType == typeof(ConditionalNode)) ConfigureConditional(visual, jsonNode);
                else if (visual.NodeType == typeof(LoopNode)) ConfigureLoop(visual, jsonNode);
                else if (visual.NodeType == typeof(RepeatTimerNode)) ConfigureRepeatTimer(visual, jsonNode);
                else if (visual.NodeType == typeof(WaitForPacketNode)) ConfigureWaitForPacket(visual, jsonNode);
                else if (visual.NodeType == typeof(SetVariableNode)) ConfigureSetVariable(visual, jsonNode);
                else if (visual.NodeType == typeof(GetVariableNode)) ConfigureGetVariable(visual, jsonNode);
                else if (visual.NodeType == typeof(AssertNode)) ConfigureAssert(visual, jsonNode);
                else if (visual.NodeType == typeof(RetryNode)) ConfigureRetry(visual, jsonNode);
            }
            catch (Exception ex)
            {
                log($"Warning: Failed to configure node {visual.NodeType?.Name}: {ex.Message}");
            }
        }

        private static void ConfigureSendPacket(NodeVisual visual, JsonElement json)
        {
            if (!json.TryGetProperty("packet_id", out var prop)) return;
            var str = prop.GetString();
            if (!string.IsNullOrEmpty(str) && Enum.TryParse<PacketId>(str, true, out var id))
                visual.Configuration!.PacketId = id;
        }

        private static void ConfigureDelay(NodeVisual visual, JsonElement json)
        {
            if (json.TryGetProperty("delay_ms", out var prop))
                visual.Configuration!.IntValue = prop.GetInt32();
        }

        private static void ConfigureRandomDelay(NodeVisual visual, JsonElement json)
        {
            if (json.TryGetProperty("min_delay_ms", out var min))
                visual.Configuration!.Properties["MinDelay"] = min.GetInt32();
            if (json.TryGetProperty("max_delay_ms", out var max))
                visual.Configuration!.Properties["MaxDelay"] = max.GetInt32();
        }

        private static void ConfigureLog(NodeVisual visual, JsonElement json)
        {
            if (json.TryGetProperty("message", out var prop))
                visual.Configuration!.StringValue = prop.GetString() ?? "";
        }

        private static void ConfigureDisconnect(NodeVisual visual, JsonElement json)
        {
            if (json.TryGetProperty("reason", out var prop))
                visual.Configuration!.StringValue = prop.GetString() ?? "User requested disconnect";
        }

        private static void ConfigureConditional(NodeVisual visual, JsonElement json)
        {
            if (!json.TryGetProperty("condition", out var prop)) return;
            var condition = prop.GetString() ?? "";
            visual.Configuration!.Properties["Left"] = condition;
            visual.Configuration!.Properties["Op"] = "==";
            visual.Configuration!.Properties["Right"] = "true";
            visual.Configuration!.Properties["LeftType"] = "Constant";
            visual.Configuration!.Properties["RightType"] = "Constant";
        }

        private static void ConfigureLoop(NodeVisual visual, JsonElement json)
        {
            if (json.TryGetProperty("max_iterations", out var prop))
                visual.Configuration!.Properties["LoopCount"] = prop.GetInt32();
        }

        private static void ConfigureRepeatTimer(NodeVisual visual, JsonElement json)
        {
            if (json.TryGetProperty("repeat_count", out var count))
                visual.Configuration!.Properties["RepeatCount"] = count.GetInt32();
            if (json.TryGetProperty("interval_ms", out var interval))
                visual.Configuration!.IntValue = interval.GetInt32();
        }

        private static void ConfigureWaitForPacket(NodeVisual visual, JsonElement json)
        {
            if (json.TryGetProperty("expected_packet_id", out var prop))
            {
                var str = prop.GetString();
                if (!string.IsNullOrEmpty(str) && Enum.TryParse<PacketId>(str, true, out var id))
                    visual.Configuration!.PacketId = id;
            }
            if (json.TryGetProperty("timeout_ms", out var timeout))
                visual.Configuration!.IntValue = timeout.GetInt32();
        }

        private static void ConfigureSetVariable(NodeVisual visual, JsonElement json)
        {
            if (json.TryGetProperty("variable_name", out var name))
                visual.Configuration!.Properties["VariableName"] = name.GetString() ?? "value";
            if (json.TryGetProperty("value_type", out var type))
                visual.Configuration!.Properties["ValueType"] = type.GetString() ?? "int";
            if (json.TryGetProperty("value", out var val))
                visual.Configuration!.Properties["Value"] = val.GetString() ?? "0";
        }

        private static void ConfigureGetVariable(NodeVisual visual, JsonElement json)
        {
            if (json.TryGetProperty("variable_name", out var prop))
                visual.Configuration!.StringValue = prop.GetString() ?? "value";
        }

        private static void ConfigureAssert(NodeVisual visual, JsonElement json)
        {
            if (json.TryGetProperty("error_message", out var msg))
                visual.Configuration!.StringValue = msg.GetString() ?? "Assertion failed";
            if (json.TryGetProperty("stop_on_failure", out var stop))
                visual.Configuration!.Properties["StopOnFailure"] = stop.GetBoolean();
        }

        private static void ConfigureRetry(NodeVisual visual, JsonElement json)
        {
            if (json.TryGetProperty("max_retries", out var maxRetries))
                visual.Configuration!.Properties["MaxRetries"] = maxRetries.GetInt32();
            if (json.TryGetProperty("retry_delay_ms", out var delay))
                visual.Configuration!.Properties["RetryDelay"] = delay.GetInt32();
            if (json.TryGetProperty("exponential_backoff", out var backoff))
                visual.Configuration!.Properties["ExponentialBackoff"] = backoff.GetBoolean();
        }

        private static BotActionGraphWindow.NodeCategory ResolveCategory(string nodeTypeName) =>
            nodeTypeName switch
            {
                _ when nodeTypeName.Contains("Condition") => BotActionGraphWindow.NodeCategory.Condition,
                _ when nodeTypeName.Contains("Loop")
                       || nodeTypeName.Contains("Repeat")
                       || nodeTypeName == "WaitForPacketNode"
                       || nodeTypeName == "RetryNode"
                       || nodeTypeName == "AssertNode" => BotActionGraphWindow.NodeCategory.Loop,
                _ => BotActionGraphWindow.NodeCategory.Action
            };

        private static Brush GetNodeColor(BotActionGraphWindow.NodeCategory c) => c switch
        {
            BotActionGraphWindow.NodeCategory.Condition => Brushes.DarkOrange,
            BotActionGraphWindow.NodeCategory.Loop => Brushes.DarkMagenta,
            _ => Brushes.DimGray
        };
    }
}
