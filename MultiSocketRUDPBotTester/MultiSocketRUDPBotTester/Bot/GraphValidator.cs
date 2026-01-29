namespace MultiSocketRUDPBotTester.Bot
{
    public enum ValidationSeverity { Error, Warning, Info }

    public class ValidationIssue
    {
        public ValidationSeverity Severity { get; set; }
        public string NodeName { get; set; } = "";
        public string Message { get; set; } = "";
        public string Category { get; set; } = "";
    }

    public class GraphValidationResult
    {
        public List<ValidationIssue> Issues { get; } = new();
        public bool IsValid => Issues.All(i => i.Severity != ValidationSeverity.Error);

        public int ErrorCount => Issues.Count(i => i.Severity == ValidationSeverity.Error);
        public int WarningCount => Issues.Count(i => i.Severity == ValidationSeverity.Warning);
        public int InfoCount => Issues.Count(i => i.Severity == ValidationSeverity.Info);

        public void AddError(string nodeName, string message, string category = "General")
        {
            Issues.Add(new ValidationIssue
            {
                Severity = ValidationSeverity.Error,
                NodeName = nodeName,
                Message = message,
                Category = category
            });
        }

        public void AddWarning(string nodeName, string message, string category = "General")
        {
            Issues.Add(new ValidationIssue
            {
                Severity = ValidationSeverity.Warning,
                NodeName = nodeName,
                Message = message,
                Category = category
            });
        }

        public void AddInfo(string nodeName, string message, string category = "General")
        {
            Issues.Add(new ValidationIssue
            {
                Severity = ValidationSeverity.Info,
                NodeName = nodeName,
                Message = message,
                Category = category
            });
        }
    }

    public static class GraphValidator
    {
        public static GraphValidationResult ValidateGraph(ActionGraph graph)
        {
            var result = new GraphValidationResult();
            var allNodes = graph.GetAllNodes();

            ValidateBasicStructure(allNodes, result);
            ValidateCycles(allNodes, result);
            ValidateNodeConfigurations(allNodes, result);
            ValidateConnectivity(allNodes, result);

            return result;
        }

        private static void ValidateBasicStructure(List<ActionNodeBase> nodes, GraphValidationResult result)
        {
            if (nodes.Count == 0)
            {
                result.AddError("Graph", "Graph has no nodes", "Structure");
                return;
            }

            var rootNodes = nodes.Where(n => n.Trigger != null).ToList();
            if (rootNodes.Count == 0)
            {
                result.AddWarning("Graph", "No trigger nodes found. Graph will not execute.", "Structure");
            }
        }

        private static void ValidateCycles(List<ActionNodeBase> nodes, GraphValidationResult result)
        {
            var visited = new HashSet<ActionNodeBase>();
            var recursionStack = new HashSet<ActionNodeBase>();

            foreach (var node in nodes.Where(n => n.Trigger != null))
            {
                if (!visited.Contains(node))
                {
                    DetectCycle(node, visited, recursionStack, new List<string>(), result, 0);
                }
            }
        }

        private static bool DetectCycle(
            ActionNodeBase node,
            HashSet<ActionNodeBase> visited,
            HashSet<ActionNodeBase> recursionStack,
            List<string> path,
            GraphValidationResult result,
            int depth)
        {
            if (depth > 500)
            {
                result.AddError(node.Name, "Graph depth exceeds 500 levels. Possible infinite recursion.", "Cycles");
                return true;
            }

            visited.Add(node);
            recursionStack.Add(node);
            path.Add(node.Name);

            var nextNodes = GetAllNextNodes(node);

            foreach (var next in nextNodes)
            {
                if (!visited.Contains(next))
                {
                    if (DetectCycle(next, visited, recursionStack, [..path], result, depth + 1))
                    {
                        return true;
                    }
                }
                else if (recursionStack.Contains(next))
                {
                    if (node is LoopNode or RepeatTimerNode)
                    {
                        result.AddInfo(node.Name, "Expected loop structure", "Cycles");
                    }
                    else
                    {
                        var cyclePath = string.Join(" → ", path) + " → " + next.Name;
                        result.AddError(node.Name, $"Circular dependency: {cyclePath}", "Cycles");
                        return true;
                    }
                }
            }

            recursionStack.Remove(node);
            return false;
        }

        private static List<ActionNodeBase> GetAllNextNodes(ActionNodeBase node)
        {
            var result = new List<ActionNodeBase>(node.NextNodes);

            switch (node)
            {
                case ConditionalNode conditional:
                    result.AddRange(conditional.TrueNodes);
                    result.AddRange(conditional.FalseNodes);
                    break;
                case LoopNode loop:
                    result.AddRange(loop.LoopBody);
                    result.AddRange(loop.ExitNodes);
                    break;
                case RepeatTimerNode repeat:
                    result.AddRange(repeat.RepeatBody);
                    break;
                case WaitForPacketNode wait:
                    result.AddRange(wait.TimeoutNodes);
                    break;
                case RetryNode retry:
                    result.AddRange(retry.RetryBody);
                    result.AddRange(retry.SuccessNodes);
                    result.AddRange(retry.FailureNodes);
                    break;
            }

            return result;
        }

        private static void ValidateNodeConfigurations(List<ActionNodeBase> nodes, GraphValidationResult result)
        {
            foreach (var node in nodes)
            {
                switch (node)
                {
                    case SendPacketNode { PacketId: PacketId.InvalidPacketId }:
                        result.AddError(node.Name, "Invalid PacketId", "Configuration");
                        break;
                    case SendPacketNode { PacketBuilder: null }:
                        result.AddError(node.Name, "PacketBuilder is null", "Configuration");
                        break;

                    case DelayNode { DelayMilliseconds: <= 0 }:
                        result.AddError(node.Name, "Delay must be positive", "Configuration");
                        break;
                    case DelayNode { DelayMilliseconds: > 60000 }:
                        result.AddWarning(node.Name, "Very long delay (>60s)", "Performance");
                        break;

                    case WaitForPacketNode { ExpectedPacketId: PacketId.InvalidPacketId }:
                        result.AddError(node.Name, "Invalid expected PacketId", "Configuration");
                        break;
                    case WaitForPacketNode { TimeoutNodes.Count: 0 }:
                        result.AddWarning(node.Name, "No timeout handler configured", "Logic");
                        break;

                    case ConditionalNode { Condition: null }:
                        result.AddError(node.Name, "Condition is null", "Configuration");
                        break;

                    case LoopNode { ContinueCondition: null }:
                        result.AddError(node.Name, "Continue condition is null", "Configuration");
                        break;
                    case LoopNode { MaxIterations: > 10000 }:
                        result.AddWarning(node.Name, "Very high iteration limit", "Performance");
                        break;
                }
            }
        }

        private static void ValidateConnectivity(List<ActionNodeBase> nodes, GraphValidationResult result)
        {
            var reachable = new HashSet<ActionNodeBase>();
            var roots = nodes.Where(n => n.Trigger != null).ToList();

            foreach (var root in roots)
            {
                CollectReachable(root, reachable);
            }

            foreach (var unreachable in nodes.Except(reachable))
            {
                result.AddWarning(unreachable.Name, "Unreachable from any trigger", "Connectivity");
            }
        }

        private static void CollectReachable(ActionNodeBase node, HashSet<ActionNodeBase> reachable)
        {
            if (!reachable.Add(node))
            {
                return;
            }

            foreach (var next in GetAllNextNodes(node))
            {
                CollectReachable(next, reachable);
            }
        }
    }
}