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
                            PacketId = packetId,
                            PacketBuilder = (_) => new NetBuffer()
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
                    else if (visual.NodeType == typeof(DisconnectNode))
                    {
                        actionNode = new DisconnectNode
                        {
                            Name = visual.NodeType.Name,
                            Reason = visual.Configuration?.StringValue ?? "User requested disconnect"
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
                    else if (visual.NodeType == typeof(SetVariableNode))
                    {
                        var variableName = visual.Configuration?.Properties.GetValueOrDefault("VariableName")?.ToString() ?? "value";
                        var valueType = visual.Configuration?.Properties.GetValueOrDefault("ValueType")?.ToString() ?? "int";
                        var value = visual.Configuration?.Properties.GetValueOrDefault("Value")?.ToString() ?? "0";

                        actionNode = new SetVariableNode
                        {
                            Name = visual.NodeType.Name,
                            VariableName = variableName,
                            ValueType = valueType,
                            StringValue = value
                        };
                    }
                    else if (visual.NodeType == typeof(GetVariableNode))
                    {
                        var variableName = visual.Configuration?.StringValue ?? "value";

                        actionNode = new GetVariableNode
                        {
                            Name = visual.NodeType.Name,
                            VariableName = variableName
                        };
                    }
                    else if (visual.NodeType == typeof(LogNode))
                    {
                        var logMessage = visual.Configuration?.StringValue ?? "No log message configured";
                        actionNode = new LogNode
                        {
                            Name = visual.NodeType.Name,
                            MessageBuilder = (_, _) => logMessage
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
                    else if (visual.NodeType == typeof(RandomChoiceNode))
                    {
                        actionNode = new RandomChoiceNode
                        {
                            Name = visual.NodeType.Name,
                            Choices = []
                        };
                    }
                    else if (visual.NodeType == typeof(AssertNode))
                    {
                        var errorMessage = visual.Configuration?.StringValue ?? "Assertion failed";
                        var stopOnFailure = visual.Configuration?.Properties.GetValueOrDefault("StopOnFailure") as bool? ?? true;

                        actionNode = new AssertNode
                        {
                            Name = visual.NodeType.Name,
                            ErrorMessage = errorMessage,
                            StopOnFailure = stopOnFailure,
                            Condition = _ => true
                        };
                    }
                    else if (visual.NodeType == typeof(RetryNode))
                    {
                        var maxRetries = visual.Configuration?.Properties.GetValueOrDefault("MaxRetries") as int? ?? 3;
                        var retryDelay = visual.Configuration?.Properties.GetValueOrDefault("RetryDelay") as int? ?? 1000;
                        var exponentialBackoff = visual.Configuration?.Properties.GetValueOrDefault("ExponentialBackoff") as bool? ?? false;

                        actionNode = new RetryNode
                        {
                            Name = visual.NodeType.Name,
                            MaxRetries = maxRetries,
                            RetryDelayMilliseconds = retryDelay,
                            UseExponentialBackoff = exponentialBackoff
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

                    if (actionNode == null)
                    {
                        Serilog.Log.Warning($"Node {visual.NodeType?.Name} could not be created, skipping");
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
                    if (nodeMapping.TryGetValue(visual.Next, out var nextNode))
                    {
                        actionNode.NextNodes.Add(nextNode);
                    }
                    else
                    {
                        Serilog.Log.Warning($"Next node not found in mapping for {actionNode.Name}");
                    }
                }

                switch (actionNode)
                {
                    case WaitForPacketNode waitNode:
                        {
                            if (visual.FalseChild != null && nodeMapping.TryGetValue(visual.FalseChild, out var timeoutNode))
                            {
                                waitNode.TimeoutNodes.Add(timeoutNode);
                            }
                            break;
                        }

                    case RandomChoiceNode randomChoiceNode:
                        {
                            if (visual.Next != null && nodeMapping.TryGetValue(visual.Next, out var choice1))
                            {
                                randomChoiceNode.Choices.Add(new ChoiceOption
                                {
                                    Name = "Choice 1",
                                    Weight = 1,
                                    Node = choice1
                                });
                            }

                            if (visual.TrueChild != null && nodeMapping.TryGetValue(visual.TrueChild, out var choice2))
                            {
                                randomChoiceNode.Choices.Add(new ChoiceOption
                                {
                                    Name = "Choice 2",
                                    Weight = 1,
                                    Node = choice2
                                });
                            }

                            if (visual.FalseChild != null && nodeMapping.TryGetValue(visual.FalseChild, out var choice3))
                            {
                                randomChoiceNode.Choices.Add(new ChoiceOption
                                {
                                    Name = "Choice 3",
                                    Weight = 1,
                                    Node = choice3
                                });
                            }
                            break;
                        }

                    case AssertNode assertNode:
                        {
                            if (visual.FalseChild != null && nodeMapping.TryGetValue(visual.FalseChild, out var failureNode))
                            {
                                assertNode.FailureNodes.Add(failureNode);
                            }
                            break;
                        }

                    case RetryNode retryNode:
                        {
                            if (visual.TrueChild != null && nodeMapping.TryGetValue(visual.TrueChild, out var bodyNode))
                            {
                                retryNode.RetryBody.Add(bodyNode);
                            }

                            if (visual.FalseChild != null && nodeMapping.TryGetValue(visual.FalseChild, out var failureNode))
                            {
                                retryNode.FailureNodes.Add(failureNode);
                            }
                            break;
                        }

                    case ConditionalNode conditionalNode:
                        {
                            if (visual.TrueChild != null && nodeMapping.TryGetValue(visual.TrueChild, out var trueNode))
                            {
                                conditionalNode.TrueNodes.Add(trueNode);
                            }

                            if (visual.FalseChild != null && nodeMapping.TryGetValue(visual.FalseChild, out var falseNode))
                            {
                                conditionalNode.FalseNodes.Add(falseNode);
                            }
                            break;
                        }

                    case LoopNode loopNode:
                        {
                            if (visual.TrueChild != null && nodeMapping.TryGetValue(visual.TrueChild, out var bodyNode))
                            {
                                loopNode.LoopBody.Add(bodyNode);
                            }

                            if (visual.FalseChild != null && nodeMapping.TryGetValue(visual.FalseChild, out var exitNode))
                            {
                                loopNode.ExitNodes.Add(exitNode);
                            }
                            break;
                        }

                    case RepeatTimerNode repeatNode:
                        {
                            if (visual.TrueChild != null && nodeMapping.TryGetValue(visual.TrueChild, out var bodyNode))
                            {
                                repeatNode.RepeatBody.Add(bodyNode);
                            }
                            break;
                        }
                }

                graph.AddNode(actionNode);
            }

            Serilog.Log.Information($"Graph built with {nodeMapping.Count} nodes");
            return graph;
        }
    }
}