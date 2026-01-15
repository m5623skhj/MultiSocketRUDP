using MultiSocketRUDPBotTester.Buffer;
using System.Windows;
using MultiSocketRUDPBotTester.Bot;

namespace MultiSocketRUDPBotTester
{
    public partial class BotActionGraphWindow : Window
    {
        private ActionGraph BuildActionGraph()
        {
            var graph = new ActionGraph { Name = "Bot Action Graph" };
            var nodeMapping = new Dictionary<NodeVisual, ActionNodeBase>();

            foreach (var visual in allNodes)
            {
                try
                {
                    ActionNodeBase? actionNode = null;
                    if (visual.IsRoot)
                    {
                        actionNode = visual.ActionNode!;
                    }
                    else if (visual.NodeType == typeof(SendPacketNode))
                    {
                        var packetId = visual.Configuration?.PacketId ?? PacketId.INVALID_PACKET_ID;
                        if (packetId == PacketId.INVALID_PACKET_ID)
                        {
                            throw new InvalidOperationException(
                                $"SendPacketNode '{visual.NodeType.Name}' requires a valid PacketId. " +
                                "Please double-click the node to configure it.");
                        }

                        actionNode = new SendPacketNode
                        {
                            Name = visual.NodeType.Name,
                            PacketId = visual.Configuration?.PacketId ?? PacketId.INVALID_PACKET_ID,
                            PacketBuilder = (_) =>
                            {
                                var buffer = new NetBuffer();
                                return buffer;
                            }
                        };
                    }
                    else if (visual.NodeType == typeof(DelayNode))
                    {
                        actionNode = new DelayNode
                        {
                            Name = visual.NodeType.Name,
                            DelayMilliseconds = visual.Configuration?.IntValue ?? 1000
                        };
                    }
                    else if (visual.NodeType == typeof(CustomActionNode))
                    {
                        actionNode = new CustomActionNode
                        {
                            Name = visual.NodeType.Name,
                            ActionHandler = (_, _) =>
                            {
                                Serilog.Log.Information($"Custom action: {visual.NodeType.Name}");
                            }
                        };
                    }
                    else if (visual.NodeType == typeof(ConditionalNode))
                    {
                        var leftType = visual.Configuration?.Properties.GetValueOrDefault("LeftType")?.ToString();
                        var left = visual.Configuration?.Properties.GetValueOrDefault("Left")?.ToString();
                        var op = visual.Configuration?.Properties.GetValueOrDefault("Op")?.ToString();
                        var rightType = visual.Configuration?.Properties.GetValueOrDefault("RightType")?.ToString();
                        var right = visual.Configuration?.Properties.GetValueOrDefault("Right")?.ToString();

                        if (left != null && op != null && right != null)
                        {
                            actionNode = new ConditionalNode
                            {
                                Name = visual.NodeType.Name,
                                Condition = ctx => EvaluateConditionWithAccessors(ctx, leftType, left, op, rightType, right)
                            };
                        }
                    }
                    else if (visual.NodeType == typeof(PacketParserNode))
                    {
                        var setterMethod = visual.Configuration?.Properties.GetValueOrDefault("SetterMethod")?.ToString();

                        if (!string.IsNullOrEmpty(setterMethod))
                        {
                            actionNode = new PacketParserNode
                            {
                                Name = visual.NodeType.Name,
                                SetterMethodName = setterMethod
                            };
                        }
                    }
                    else if (visual.NodeType == typeof(LoopNode))
                    {
                        var count = visual.Configuration?.Properties.GetValueOrDefault("LoopCount") as int? ?? 1;

                        actionNode = new LoopNode
                        {
                            Name = visual.NodeType.Name,
                            ContinueCondition = ctx =>
                            {
                                var i = ctx.GetOrDefault("LoopIndex", 0) + 1;
                                ctx.Set("LoopIndex", i);
                                return i < count;
                            },
                            MaxIterations = count
                        };
                    }
                    else if (visual.NodeType == typeof(RepeatTimerNode))
                    {
                        actionNode = new RepeatTimerNode
                        {
                            Name = visual.NodeType.Name,
                            IntervalMilliseconds = visual.Configuration?.IntValue ?? 1000,
                            RepeatCount = visual.Configuration?.Properties.ContainsKey("RepeatCount") == true
                                ? (int)visual.Configuration.Properties["RepeatCount"]
                                : 10
                        };
                    }
                    else if (visual.NodeType == typeof(LogNode))
                    {
                        actionNode = new LogNode
                        {
                            Name = visual.NodeType.Name,
                            MessageBuilder = (_, _) => $"Log from {visual.NodeType.Name}"
                        };
                    }
                    else if (visual.NodeType != null)
                    {
                        actionNode = new CustomActionNode
                        {
                            Name = visual.NodeType.Name,
                            ActionHandler = (_, _) =>
                            {
                                Serilog.Log.Information($"Executing node: {visual.NodeType.Name}");
                            }
                        };
                    }
                    else if (visual.NodeType == typeof(DisconnectNode))
                    {
                        actionNode = new DisconnectNode
                        {
                            Name = visual.NodeType.Name,
                            Reason = visual.Configuration?.StringValue ?? "User requested disconnect"
                        };
                    }
                    else if (visual.NodeType == typeof(RandomDelayNode))
                    {
                        var minDelay = visual.Configuration?.Properties.GetValueOrDefault("MinDelay") as int? ?? 500;
                        var maxDelay = visual.Configuration?.Properties.GetValueOrDefault("MaxDelay") as int? ?? 2000;

                        actionNode = new RandomDelayNode
                        {
                            Name = visual.NodeType.Name,
                            MinDelayMilliseconds = minDelay,
                            MaxDelayMilliseconds = maxDelay
                        };
                    }
                    else if (visual.NodeType == typeof(WaitForPacketNode))
                    {
                        var packetId = visual.Configuration?.PacketId ?? PacketId.INVALID_PACKET_ID;
                        var timeout = visual.Configuration?.IntValue ?? 5000;

                        if (packetId == PacketId.INVALID_PACKET_ID)
                        {
                            throw new InvalidOperationException(
                                "WaitForPacketNode requires a valid PacketId. " +
                                "Please double-click the node to configure it.");
                        }

                        actionNode = new WaitForPacketNode
                        {
                            Name = visual.NodeType.Name,
                            ExpectedPacketId = packetId,
                            TimeoutMilliseconds = timeout
                        };
                    }
                    else if (visual.NodeType == typeof(WaitForPacketNode.SetVariableNode))
                    {
                        var variableName = visual.Configuration?.Properties.GetValueOrDefault("VariableName")?.ToString() ?? "value";
                        var valueType = visual.Configuration?.Properties.GetValueOrDefault("ValueType")?.ToString() ?? "int";
                        var value = visual.Configuration?.Properties.GetValueOrDefault("Value")?.ToString() ?? "0";

                        actionNode = new WaitForPacketNode.SetVariableNode
                        {
                            Name = visual.NodeType.Name,
                            VariableName = variableName,
                            ValueType = valueType,
                            StringValue = value
                        };
                    }
                    else if (visual.NodeType == typeof(WaitForPacketNode.GetVariableNode))
                    {
                        var variableName = visual.Configuration?.StringValue ?? "value";

                        actionNode = new WaitForPacketNode.GetVariableNode
                        {
                            Name = visual.NodeType.Name,
                            VariableName = variableName
                        };
                    }

                    if (actionNode is WaitForPacketNode waitNode)
                    {
                        if (visual.FalseChild != null)
                        {
                            if (nodeMapping.TryGetValue(visual.FalseChild, out var timeoutNode))
                            {
                                waitNode.TimeoutNodes.Add(timeoutNode);
                            }
                            else
                            {
                                Serilog.Log.Warning($"Timeout node not found in mapping for {actionNode.Name}");
                            }
                        }

                        if (visual.TrueChild != null)
                        {
                            if (nodeMapping.TryGetValue(visual.TrueChild, out var successNode))
                            {
                                actionNode.NextNodes.Add(successNode);
                            }
                            else
                            {
                                Serilog.Log.Warning($"Success node not found in mapping for {actionNode.Name}");
                            }
                        }
                    }
                    else
                    {
                        Serilog.Log.Warning("Node has no type information, skipping");
                        continue;
                    }

                    nodeMapping[visual] = actionNode;
                    visual.ActionNode = actionNode;
                }
                catch (Exception ex)
                {
                    Serilog.Log.Error($"Error creating action node for {visual.NodeType?.Name}: {ex.Message}");
                    throw new Exception($"Failed to create node '{visual.NodeType?.Name}': {ex.Message}");
                }
            }

            foreach (var visual in allNodes)
            {
                if (!nodeMapping.TryGetValue(visual, out var actionNode))
                {
                    Serilog.Log.Warning("Visual node not in mapping, skipping connections");
                    continue;
                }

                if (visual.Next != null)
                {
                    if (nodeMapping.TryGetValue(visual.Next, out var value))
                    {
                        actionNode.NextNodes.Add(value);
                    }
                    else
                    {
                        Serilog.Log.Warning($"Next node not found in mapping for {actionNode.Name}");
                    }
                }

                if (actionNode is ConditionalNode conditionalNode)
                {
                    if (visual.TrueChild != null)
                    {
                        if (nodeMapping.TryGetValue(visual.TrueChild, out var value))
                        {
                            conditionalNode.TrueNodes.Add(value);
                        }
                        else
                        {
                            Serilog.Log.Warning($"TrueChild not found in mapping for {actionNode.Name}");
                        }
                    }

                    if (visual.FalseChild != null)
                    {
                        if (nodeMapping.TryGetValue(visual.FalseChild, out var value))
                        {
                            conditionalNode.FalseNodes.Add(value);
                        }
                        else
                        {
                            Serilog.Log.Warning($"FalseChild not found in mapping for {actionNode.Name}");
                        }
                    }
                }

                if (actionNode is LoopNode loopNode)
                {
                    if (visual.TrueChild != null)
                    {
                        if (nodeMapping.TryGetValue(visual.TrueChild, out var value))
                        {
                            loopNode.LoopBody.Add(value);
                        }
                        else
                        {
                            Serilog.Log.Warning($"LoopBody child not found in mapping for {actionNode.Name}");
                        }
                    }

                    if (visual.FalseChild != null)
                    {
                        if (nodeMapping.TryGetValue(visual.FalseChild, out var value))
                        {
                            loopNode.ExitNodes.Add(value);
                        }
                        else
                        {
                            Serilog.Log.Warning($"ExitNode child not found in mapping for {actionNode.Name}");
                        }
                    }
                }

                if (actionNode is RepeatTimerNode repeatNode)
                {
                    if (visual.TrueChild != null)
                    {
                        if (nodeMapping.TryGetValue(visual.TrueChild, out var value))
                        {
                            repeatNode.RepeatBody.Add(value);
                        }
                        else
                        {
                            Serilog.Log.Warning($"RepeatBody child not found in mapping for {actionNode.Name}");
                        }
                    }
                }

                graph.AddNode(actionNode);
            }

            Serilog.Log.Information($"Graph built with {nodeMapping.Count} nodes");
            return graph;
        }
    }
}
