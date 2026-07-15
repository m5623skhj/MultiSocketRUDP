using System.Text.Json;
using MultiSocketRUDPBotTester.Bot;
using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.Contents.Client;
using MultiSocketRUDPBotTester.Graph;
using MultiSocketRUDPBotTester.Graph.Builders;
using static MultiSocketRUDPBotTester.BotActionGraphWindow;

namespace MultiSocketRUDPBotTester.UnitTests;

public sealed class GraphValidationTests
{
    /// <summary>
    /// graph validation 결과가 오류, 경고 및 정보 심각도별 개수와 category를 집계하는지 확인합니다.
    /// </summary>
    [Fact]
    public void ValidationResultCountsSeverities()
    {
        var result = new GraphValidationResult();
        result.AddError("a", "error", "structure");
        result.AddWarning("b", "warning");
        result.AddInfo("c", "info");

        Assert.False(result.IsValid);
        Assert.Equal(1, result.ErrorCount);
        Assert.Equal(1, result.WarningCount);
        Assert.Equal(1, result.InfoCount);
        Assert.Equal("structure", result.Issues[0].Category);
    }

    /// <summary>
    /// 비어 있거나 trigger root가 없는 graph가 구조 및 접근성 문제를 보고하는지 확인합니다.
    /// </summary>
    [Fact]
    public void EmptyAndRootlessGraphsReportStructureProblems()
    {
        var empty = GraphValidator.ValidateGraph(new ActionGraph());
        Assert.Contains(empty.Issues, issue => issue.Severity == ValidationSeverity.Error && issue.Message.Contains("no nodes"));

        var rootless = new ActionGraph();
        rootless.AddNode(new TestNode { Name = "orphan" });
        var result = GraphValidator.ValidateGraph(rootless);
        Assert.Contains(result.Issues, issue => issue.Message.Contains("No trigger nodes"));
        Assert.Contains(result.Issues, issue => issue.NodeName == "orphan" && issue.Message.Contains("Unreachable"));
    }

    /// <summary>
    /// 접근성 검사가 조건, 반복, 대기, 검증, 재시도 및 무작위 분기의 모든 자식 형태를 순회하는지 확인합니다.
    /// </summary>
    [Fact]
    public void ReachabilityTraversesEverySpecialBranchShape()
    {
        var root = Root("root");
        var conditional = new ConditionalNode { Name = "conditional", Condition = _ => true };
        var loop = new LoopNode { Name = "loop", ContinueCondition = _ => false };
        var repeat = new RepeatTimerNode { Name = "repeat" };
        var wait = new WaitForPacketNode { Name = "wait", ExpectedPacketId = PacketId.Ping };
        var assertion = new AssertNode { Name = "assert", Condition = _ => true };
        var retry = new RetryNode { Name = "retry" };
        var random = new RandomChoiceNode
        {
            Name = "random",
            Choices =
            [
                new ChoiceOption { Name = "a", Node = new TestNode { Name = "choice-a" } },
                new ChoiceOption { Name = "b", Node = new TestNode { Name = "choice-b" } }
            ]
        };
        root.NextNodes.Add(conditional);
        conditional.TrueNodes.Add(loop);
        conditional.FalseNodes.Add(new TestNode { Name = "false" });
        loop.LoopBody.Add(repeat);
        loop.ExitNodes.Add(new TestNode { Name = "exit" });
        repeat.RepeatBody.Add(wait);
        wait.TimeoutNodes.Add(assertion);
        wait.NextNodes.Add(new TestNode { Name = "wait-success" });
        assertion.FailureNodes.Add(retry);
        retry.RetryBody.Add(random);
        retry.SuccessNodes.Add(new TestNode { Name = "retry-success" });
        retry.FailureNodes.Add(new TestNode { Name = "retry-failure" });

        var graph = new ActionGraph();
        foreach (var node in Enumerate(root))
        {
            graph.AddNode(node);
        }

        var result = GraphValidator.ValidateGraph(graph);

        Assert.DoesNotContain(result.Issues, issue => issue.Category == "Connectivity");
    }

    /// <summary>
    /// loop가 아닌 순환 연결과 허용 깊이를 초과한 graph를 validation 오류로 보고하는지 확인합니다.
    /// </summary>
    [Fact]
    public void NonLoopCycleAndExcessiveDepthAreErrors()
    {
        var first = Root("first");
        var second = new TestNode { Name = "second" };
        first.NextNodes.Add(second);
        second.NextNodes.Add(first);
        var cyclic = new ActionGraph();
        cyclic.AddNode(first);
        cyclic.AddNode(second);
        Assert.Contains(GraphValidator.ValidateGraph(cyclic).Issues, issue => issue.Message.Contains("Circular dependency"));

        var deep = new ActionGraph();
        var deepRoot = Root("0");
        deep.AddNode(deepRoot);
        var current = deepRoot;
        for (var i = 1; i <= 502; i++)
        {
            var next = new TestNode { Name = i.ToString() };
            current.NextNodes.Add(next);
            deep.AddNode(next);
            current = next;
        }
        Assert.Contains(GraphValidator.ValidateGraph(deep).Issues, issue => issue.Message.Contains("depth exceeds 500"));
    }

    /// <summary>
    /// 각 node 종류의 필수 설정 및 권장 범위 위반이 적절한 오류와 경고로 보고되는지 확인합니다.
    /// </summary>
    [Fact]
    public void NodeConfigurationRulesReportErrorsAndWarnings()
    {
        var root = Root("root");
        var nodes = new ActionNodeBase[]
        {
            new SendPacketNode { Name = "send-invalid", PacketId = PacketId.InvalidPacketId },
            new DelayNode { Name = "delay-zero", DelayMilliseconds = 0 },
            new DelayNode { Name = "delay-long", DelayMilliseconds = 60_001 },
            new WaitForPacketNode { Name = "wait-invalid" },
            new WaitForPacketNode { Name = "wait-no-timeout", ExpectedPacketId = PacketId.Ping },
            new ConditionalNode { Name = "conditional-null" },
            new LoopNode { Name = "loop-null" },
            new LoopNode { Name = "loop-high", ContinueCondition = _ => false, MaxIterations = 10_001 },
            new AssertNode { Name = "assert-null" },
            new AssertNode { Name = "assert-no-failure", Condition = _ => true },
            new RandomChoiceNode { Name = "random", Choices = [new ChoiceOption()] }
        };
        root.NextNodes.Add(nodes[0]);
        for (var i = 0; i < nodes.Length - 1; i++)
        {
            nodes[i].NextNodes.Add(nodes[i + 1]);
        }
        var graph = new ActionGraph();
        graph.AddNode(root);
        foreach (var node in nodes)
        {
            graph.AddNode(node);
        }

        var result = GraphValidator.ValidateGraph(graph);

        Assert.Contains(result.Issues, issue => issue.NodeName == "send-invalid" && issue.Message.Contains("Invalid PacketId"));
        Assert.Contains(result.Issues, issue => issue.NodeName == "delay-zero" && issue.Severity == ValidationSeverity.Error);
        Assert.Contains(result.Issues, issue => issue.NodeName == "delay-long" && issue.Severity == ValidationSeverity.Warning);
        Assert.Contains(result.Issues, issue => issue.NodeName == "wait-invalid" && issue.Severity == ValidationSeverity.Error);
        Assert.Contains(result.Issues, issue => issue.NodeName == "wait-no-timeout" && issue.Severity == ValidationSeverity.Warning);
        Assert.Contains(result.Issues, issue => issue.NodeName == "conditional-null" && issue.Severity == ValidationSeverity.Error);
        Assert.Contains(result.Issues, issue => issue.NodeName == "loop-high" && issue.Severity == ValidationSeverity.Warning);
        Assert.Contains(result.Issues, issue => issue.NodeName == "assert-no-failure" && issue.Severity == ValidationSeverity.Warning);
        Assert.Contains(result.Issues, issue => issue.NodeName == "random" && issue.Severity == ValidationSeverity.Error);
    }

    private static TestNode Root(string name) => new()
    {
        Name = name,
        Trigger = new TriggerCondition { Type = TriggerType.OnConnected }
    };

    private static IEnumerable<ActionNodeBase> Enumerate(ActionNodeBase root)
    {
        var found = new HashSet<ActionNodeBase>();
        var pending = new Stack<ActionNodeBase>();
        pending.Push(root);
        while (pending.TryPop(out var node))
        {
            if (!found.Add(node))
            {
                continue;
            }
            yield return node;
            foreach (var child in Children(node))
            {
                pending.Push(child);
            }
        }
    }

    private static IEnumerable<ActionNodeBase> Children(ActionNodeBase node)
    {
        foreach (var next in node.NextNodes) yield return next;
        if (node is ConditionalNode conditional)
        {
            foreach (var next in conditional.TrueNodes.Concat(conditional.FalseNodes)) yield return next;
        }
        if (node is LoopNode loop)
        {
            foreach (var next in loop.LoopBody.Concat(loop.ExitNodes)) yield return next;
        }
        if (node is RepeatTimerNode repeat)
        {
            foreach (var next in repeat.RepeatBody) yield return next;
        }
        if (node is WaitForPacketNode wait)
        {
            foreach (var next in wait.TimeoutNodes) yield return next;
        }
        if (node is AssertNode assertion)
        {
            foreach (var next in assertion.FailureNodes) yield return next;
        }
        if (node is RetryNode retry)
        {
            foreach (var next in retry.RetryBody.Concat(retry.SuccessNodes).Concat(retry.FailureNodes)) yield return next;
        }
        if (node is RandomChoiceNode random)
        {
            foreach (var next in random.Choices.Where(choice => choice.Node != null).Select(choice => choice.Node!)) yield return next;
        }
    }
}

public sealed class ActionGraphBuilderTests
{
    /// <summary>
    /// fluent builder가 이름 있는 선형 graph를 만들고 외부 변경으로부터 node 목록을 보호하는지 확인합니다.
    /// </summary>
    [Fact]
    public void FluentBuilderCreatesNamedLinearGraphAndReturnsDefensiveNodeList()
    {
        var graph = new ActionGraphBuilder()
            .WithName("sample")
            .OnConnected("root")
            .ThenDo("action", (_, _) => { })
            .ThenWait("delay", 25)
            .Build();

        var nodes = graph.GetAllNodes();
        Assert.Equal("sample", graph.Name);
        Assert.Equal(3, nodes.Count);
        Assert.Equal("action", Assert.Single(nodes[0].NextNodes).Name);
        Assert.IsType<DelayNode>(Assert.Single(nodes[1].NextNodes));
        nodes.Clear();
        Assert.Equal(3, graph.GetAllNodes().Count);
    }

    /// <summary>
    /// fluent builder가 조건, loop 및 반복 timer의 분기 구조를 올바르게 연결하는지 확인합니다.
    /// </summary>
    [Fact]
    public void FluentBuilderCreatesConditionalLoopAndRepeatBranchStructures()
    {
        var graph = new ActionGraphBuilder()
            .OnReceive("root", PacketId.Ping)
            .ThenIf("condition", _ => true)
                .TrueDo("true", (_, _) => { })
                .FalseDo("false", (_, _) => { })
                .EndIf()
            .ThenLoop("loop", _ => false, 3)
                .Do("body", (_, _) => { })
                .OnExit("exit", (_, _) => { })
                .EndLoop()
            .ThenRepeat("repeat", 2, 10)
                .Do("repeat-body", (_, _) => { })
                .EndRepeat()
            .Build();

        var conditional = Assert.IsType<ConditionalNode>(graph.GetAllNodes().Single(node => node.Name == "condition"));
        Assert.Equal("true", Assert.Single(conditional.TrueNodes).Name);
        Assert.Equal("false", Assert.Single(conditional.FalseNodes).Name);
        var loop = Assert.IsType<LoopNode>(graph.GetAllNodes().Single(node => node.Name == "loop"));
        Assert.Equal(3, loop.MaxIterations);
        Assert.Equal("body", Assert.Single(loop.LoopBody).Name);
        Assert.Equal("exit", Assert.Single(loop.ExitNodes).Name);
        var repeat = Assert.IsType<RepeatTimerNode>(graph.GetAllNodes().Single(node => node.Name == "repeat"));
        Assert.Equal(2, repeat.RepeatCount);
        Assert.Equal(10, repeat.IntervalMilliseconds);
        Assert.Equal("repeat-body", Assert.Single(repeat.RepeatBody).Name);
    }
}

public sealed class NodeBuilderTests
{
    /// <summary>
    /// node 설정 reader가 지원 형식을 변환하고 누락·오류 값에는 기본값을 사용하는지 확인합니다.
    /// </summary>
    [Fact]
    public void ConfigurationReaderConvertsSupportedValuesAndFallsBack()
    {
        var visual = Visual(typeof(DelayNode), new NodeConfiguration
        {
            Properties =
            {
                ["int"] = "42",
                ["long"] = 7L,
                ["double"] = 3.9,
                ["true"] = "true",
                ["one"] = 1,
                ["bad"] = new object()
            }
        });

        Assert.Equal(42, NodeConfigurationValueReader.GetInt(visual, "int", -1));
        Assert.Equal(7, NodeConfigurationValueReader.GetInt(visual, "long", -1));
        Assert.Equal(3, NodeConfigurationValueReader.GetInt(visual, "double", -1));
        Assert.Equal(-1, NodeConfigurationValueReader.GetInt(visual, "bad", -1));
        Assert.Equal(-1, NodeConfigurationValueReader.GetInt(visual, "missing", -1));
        Assert.True(NodeConfigurationValueReader.GetBool(visual, "true", false));
        Assert.True(NodeConfigurationValueReader.GetBool(visual, "one", false));
        Assert.False(NodeConfigurationValueReader.GetBool(visual, "bad", false));
    }

    /// <summary>
    /// 송신 node builder가 PacketId를 필수로 검사하고 schema 기반 필드를 파싱하는지 확인합니다.
    /// </summary>
    [Fact]
    public void SendPacketBuilderRequiresPacketIdAndParsesSchemaFields()
    {
        var builder = new SendPacketNodeBuilder();
        Assert.Throws<InvalidOperationException>(() => builder.Build(Visual(typeof(SendPacketNode))));
        var visual = Visual(typeof(SendPacketNode), new NodeConfiguration
        {
            PacketId = PacketId.TestPacketReq,
            Properties = { ["Field_order"] = "123" }
        });

        var node = Assert.IsType<SendPacketNode>(builder.Build(visual));
        Assert.Equal(PacketId.TestPacketReq, node.PacketId);
        Assert.Equal(123, node.FieldValues["order"]);
    }

    /// <summary>
    /// 단순 node builder들이 설정값과 기본값을 대상 node 속성에 올바르게 반영하는지 확인합니다.
    /// </summary>
    [Fact]
    public void SimpleBuildersUseConfiguredAndDefaultValues()
    {
        var delay = Assert.IsType<DelayNode>(new DelayNodeBuilder().Build(Visual(typeof(DelayNode))));
        Assert.Equal(1000, delay.DelayMilliseconds);
        var random = Assert.IsType<RandomDelayNode>(new RandomDelayNodeBuilder().Build(Visual(
            typeof(RandomDelayNode),
            new NodeConfiguration { Properties = { ["MinDelay"] = 10, ["MaxDelay"] = 20 } })));
        Assert.Equal(10, random.MinDelayMilliseconds);
        Assert.Equal(20, random.MaxDelayMilliseconds);
        var disconnect = Assert.IsType<DisconnectNode>(new DisconnectNodeBuilder().Build(Visual(typeof(DisconnectNode))));
        Assert.Equal("User requested disconnect", disconnect.Reason);
    }

    /// <summary>
    /// 제어 node builder가 필수 값을 검증하고 evaluator에 전달할 인자를 캡처하는지 확인합니다.
    /// </summary>
    [Fact]
    public void ControlBuildersValidateRequiredValuesAndCaptureEvaluatorArguments()
    {
        string? captured = null;
        var conditionalBuilder = new ConditionalNodeBuilder((_, leftType, left, op, rightType, right) =>
        {
            captured = $"{leftType}|{left}|{op}|{rightType}|{right}";
            return true;
        });
        Assert.Throws<InvalidOperationException>(() => conditionalBuilder.Build(Visual(typeof(ConditionalNode))));
        var conditional = Assert.IsType<ConditionalNode>(conditionalBuilder.Build(Visual(
            typeof(ConditionalNode),
            new NodeConfiguration
            {
                Properties =
                {
                    ["LeftType"] = "Constant", ["Left"] = "1", ["Op"] = "==",
                    ["RightType"] = "Constant", ["Right"] = "1"
                }
            })));
        Assert.True(conditional.Condition!(new RuntimeContext(null!, null)));
        Assert.Equal("Constant|1|==|Constant|1", captured);

        var loop = Assert.IsType<LoopNode>(new LoopNodeBuilder().Build(Visual(
            typeof(LoopNode), new NodeConfiguration { Properties = { ["LoopCount"] = 2 } })));
        var context = new RuntimeContext(null!, null);
        Assert.True(loop.ContinueCondition!(context));
        Assert.True(loop.ContinueCondition(context));
        Assert.False(loop.ContinueCondition(context));
        Assert.Equal(2, loop.MaxIterations);
    }

    /// <summary>
    /// 나머지 node builder들이 시각 설정을 각각의 실행 node 속성으로 정확히 매핑하는지 확인합니다.
    /// </summary>
    [Fact]
    public void RemainingBuildersMapConfigurationToNodeProperties()
    {
        var waitBuilder = new WaitForPacketNodeBuilder();
        Assert.Throws<InvalidOperationException>(() => waitBuilder.Build(Visual(typeof(WaitForPacketNode))));
        var wait = Assert.IsType<WaitForPacketNode>(waitBuilder.Build(Visual(
            typeof(WaitForPacketNode), new NodeConfiguration { PacketId = PacketId.Pong, IntValue = 250 })));
        Assert.Equal(PacketId.Pong, wait.ExpectedPacketId);
        Assert.Equal(250, wait.TimeoutMilliseconds);

        var set = Assert.IsType<SetVariableNode>(new SetVariableNodeBuilder().Build(Visual(
            typeof(SetVariableNode), new NodeConfiguration
            {
                Properties = { ["VariableName"] = "score", ["ValueType"] = "long", ["Value"] = "9" }
            })));
        Assert.Equal("score", set.VariableName);
        Assert.Equal("long", set.ValueType);
        Assert.Equal("9", set.StringValue);

        var parserBuilder = new PacketParserNodeBuilder();
        Assert.Throws<InvalidOperationException>(() => parserBuilder.Build(Visual(typeof(PacketParserNode))));
        var parser = Assert.IsType<PacketParserNode>(parserBuilder.Build(Visual(
            typeof(PacketParserNode), new NodeConfiguration { Properties = { ["SetterMethod"] = "Parse" } })));
        Assert.Equal("Parse", parser.SetterMethodName);

        var repeat = Assert.IsType<RepeatTimerNode>(new RepeatTimerNodeBuilder().Build(Visual(
            typeof(RepeatTimerNode), new NodeConfiguration { IntValue = 50, Properties = { ["RepeatCount"] = 4 } })));
        Assert.Equal(50, repeat.IntervalMilliseconds);
        Assert.Equal(4, repeat.RepeatCount);

        var retry = Assert.IsType<RetryNode>(new RetryNodeBuilder().Build(Visual(
            typeof(RetryNode), new NodeConfiguration
            {
                Properties = { ["MaxRetries"] = 5, ["RetryDelay"] = 25, ["ExponentialBackoff"] = true }
            })));
        Assert.Equal(5, retry.MaxRetries);
        Assert.Equal(25, retry.RetryDelayMilliseconds);
        Assert.True(retry.UseExponentialBackoff);
    }

    /// <summary>
    /// builder registry가 알려진 형식을 생성하고 누락·알 수 없는 형식을 계약대로 처리하는지 확인합니다.
    /// </summary>
    [Fact]
    public void RegistryBuildsKnownTypeReturnsNullForMissingTypeAndFallsBackForUnknownType()
    {
        var registry = new NodeBuilderRegistry((_, _, _, _, _, _) => true);

        Assert.IsType<DelayNode>(registry.TryBuild(Visual(typeof(DelayNode))));
        Assert.Null(registry.TryBuild(new NodeVisual()));
        var fallback = Assert.IsType<CustomActionNode>(registry.TryBuild(Visual(typeof(TestNode))));
        Assert.Equal(nameof(TestNode), fallback.Name);
    }

    private static NodeVisual Visual(Type type, NodeConfiguration? configuration = null) => new()
    {
        NodeType = type,
        Configuration = configuration
    };
}

public sealed class GraphFileStorageTests
{
    /// <summary>
    /// graph 저장과 로드가 enum, node 연결 및 JSON 속성을 손실 없이 왕복하는지 확인합니다.
    /// </summary>
    [Fact]
    public void SaveLoadRoundTripsEnumsConnectionsAndJsonProperties()
    {
        var directory = Path.Combine(Path.GetTempPath(), $"bot-graph-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        var path = Path.Combine(directory, GraphFileStorage.DefaultFileName);
        try
        {
            var graph = new GraphFileModel
            {
                Name = "sample",
                Nodes =
                [
                    new NodeVisualFileModel
                    {
                        Id = 1,
                        IsRoot = true,
                        NodeTypeName = nameof(TestNode),
                        Category = NodeCategory.Action,
                        Left = 12.5,
                        Top = 8.25,
                        NextNodeId = 2,
                        DynamicPortTypes = ["choice"],
                        DynamicChildIds = [2],
                        Configuration = new NodeConfigurationFileModel
                        {
                            PacketId = PacketId.Ping,
                            StringValue = "value",
                            IntValue = 7,
                            Properties = { ["flag"] = JsonSerializer.SerializeToElement(true) }
                        }
                    }
                ]
            };

            GraphFileStorage.Save(path, graph);
            var loaded = GraphFileStorage.Load(path);

            Assert.Equal("sample", loaded.Name);
            var node = Assert.Single(loaded.Nodes);
            Assert.Equal(NodeCategory.Action, node.Category);
            Assert.Equal(2, node.NextNodeId);
            Assert.Equal("choice", Assert.Single(node.DynamicPortTypes));
            Assert.Equal(2, Assert.Single(node.DynamicChildIds));
            Assert.True(node.Configuration!.Properties["flag"].GetBoolean());
            Assert.Contains("\"Action\"", File.ReadAllText(path));
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }

    /// <summary>
    /// 비어 있거나 잘못된 파일 및 존재하지 않는 경로의 graph 로드를 거부하는지 확인합니다.
    /// </summary>
    [Fact]
    public void LoadRejectsEmptyMalformedAndMissingFiles()
    {
        var directory = Path.Combine(Path.GetTempPath(), $"bot-graph-{Guid.NewGuid():N}");
        Directory.CreateDirectory(directory);
        try
        {
            var path = Path.Combine(directory, "graph.json");
            GraphFileStorage.Save(path, new GraphFileModel());
            Assert.Throws<InvalidOperationException>(() => GraphFileStorage.Load(path));
            File.WriteAllText(path, "{broken}");
            Assert.Throws<JsonException>(() => GraphFileStorage.Load(path));
            Assert.Throws<FileNotFoundException>(() => GraphFileStorage.Load(Path.Combine(directory, "missing.json")));
        }
        finally
        {
            Directory.Delete(directory, true);
        }
    }
}

internal sealed class TestNode : ActionNodeBase
{
    public override void Execute(Client client, NetBuffer? receivedPacket = null)
    {
    }
}
