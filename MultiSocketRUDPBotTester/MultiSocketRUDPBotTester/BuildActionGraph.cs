using System.Windows;
using MultiSocketRUDPBotTester.Bot;
using MultiSocketRUDPBotTester.Graph;

namespace MultiSocketRUDPBotTester
{
    public partial class BotActionGraphWindow : Window
    {
        private ActionGraph BuildActionGraph()
        {
            var graph = new ActionGraph { Name = "Bot Action Graph" };
            var mapping = new Dictionary<NodeVisual, ActionNodeBase>();
            var registry = new NodeBuilderRegistry(EvaluateConditionWithAccessors);

            foreach (var visual in allNodes)
            {
                try
                {
                    var actionNode = visual.IsRoot
                        ? visual.ActionNode!
                        : registry.TryBuild(visual);

                    if (actionNode == null)
                    {
                        Serilog.Log.Warning("Node {NodeType} could not be created, skipping", visual.NodeType?.Name);
                        continue;
                    }

                    mapping[visual] = actionNode;
                    visual.ActionNode = actionNode;
                }
                catch (Exception ex)
                {
                    throw new Exception($"Failed to create node '{visual.NodeType?.Name}': {ex.Message}");
                }
            }

            foreach (var visual in allNodes)
            {
                if (!mapping.TryGetValue(visual, out var actionNode))
                {
                    Serilog.Log.Warning("Visual not in mapping, skipping connections");
                    continue;
                }

                ConnectNext(visual, actionNode, mapping);
                ConnectBranches(visual, actionNode, mapping);

                if (actionNode.Trigger != null)
                {
                    graph.AddNode(actionNode);
                }
            }

            Serilog.Log.Information("Graph built with {Count} nodes", mapping.Count);
            return graph;
        }

        private static void ConnectNext(
            NodeVisual visual,
            ActionNodeBase actionNode,
            Dictionary<NodeVisual, ActionNodeBase> mapping)
        {
            if (visual.Next == null)
                return;

            if (mapping.TryGetValue(visual.Next, out var nextNode))
                actionNode.NextNodes.Add(nextNode);
            else
                Serilog.Log.Warning("Next node not found in mapping for {Name}", actionNode.Name);
        }

        private static void ConnectBranches(
            NodeVisual visual,
            ActionNodeBase actionNode,
            Dictionary<NodeVisual, ActionNodeBase> mapping)
        {
            switch (actionNode)
            {
                case ConditionalNode conditional:
                    if (visual.TrueChild != null && mapping.TryGetValue(visual.TrueChild, out var trueNode))
                        conditional.TrueNodes.Add(trueNode);
                    if (visual.FalseChild != null && mapping.TryGetValue(visual.FalseChild, out var falseNode))
                        conditional.FalseNodes.Add(falseNode);
                    break;

                case LoopNode loop:
                    if (visual.TrueChild != null && mapping.TryGetValue(visual.TrueChild, out var loopBody))
                        loop.LoopBody.Add(loopBody);
                    if (visual.FalseChild != null && mapping.TryGetValue(visual.FalseChild, out var exitNode))
                        loop.ExitNodes.Add(exitNode);
                    break;

                case RepeatTimerNode repeat:
                    if (visual.TrueChild != null && mapping.TryGetValue(visual.TrueChild, out var repeatBody))
                        repeat.RepeatBody.Add(repeatBody);
                    break;

                case WaitForPacketNode wait:
                    if (visual.TrueChild != null && mapping.TryGetValue(visual.TrueChild, out var waitSuccess))
                        wait.NextNodes.Add(waitSuccess);
                    if (visual.FalseChild != null && mapping.TryGetValue(visual.FalseChild, out var timeoutNode))
                        wait.TimeoutNodes.Add(timeoutNode);
                    break;

                case AssertNode assert:
                    if (visual.TrueChild != null && mapping.TryGetValue(visual.TrueChild, out var assertSuccess))
                        assert.NextNodes.Add(assertSuccess);
                    if (visual.FalseChild != null && mapping.TryGetValue(visual.FalseChild, out var failNode))
                        assert.FailureNodes.Add(failNode);
                    break;

                case RetryNode retry:
                    if (visual.TrueChild != null && mapping.TryGetValue(visual.TrueChild, out var retryBody))
                        retry.RetryBody.Add(retryBody);
                    if (visual.FalseChild != null && mapping.TryGetValue(visual.FalseChild, out var retryFail))
                        retry.FailureNodes.Add(retryFail);
                    break;

                case RandomChoiceNode randomChoice:
                    for (var i = 0; i < visual.DynamicChildren.Count; i++)
                    {
                        var childVisual = visual.DynamicChildren[i];
                        if (childVisual != null && mapping.TryGetValue(childVisual, out var choiceNode))
                        {
                            randomChoice.Choices.Add(new ChoiceOption
                            {
                                Name = $"Choice {i + 1}",
                                Weight = 1,
                                Node = choiceNode
                            });
                        }
                    }
                    break;
            }
        }
    }
}
