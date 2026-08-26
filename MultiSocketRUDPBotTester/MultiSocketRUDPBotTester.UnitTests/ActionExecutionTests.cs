using MultiSocketRUDPBotTester.Bot;
using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.Contents.Client;
using static MultiSocketRUDPBotTester.Bot.NodeExecutionStats;

namespace MultiSocketRUDPBotTester.UnitTests;

public sealed class ActionExecutionTests
{
    /// <summary>
    /// 선형 노드 체인을 한 번씩 실행하고 순환 경로에서 재진입을 중단하는지 확인합니다.
    /// </summary>
    [Fact]
    public void ExecuteChainTraversesLinearNodesOnceAndStopsCycles()
    {
        var calls = new List<string>();
        var first = Count("first", calls);
        var second = Count("second", calls);
        var third = Count("third", calls);
        first.NextNodes.Add(second);
        second.NextNodes.Add(third);
        third.NextNodes.Add(first);
        var context = new RuntimeContext(null!, null);

        NodeExecutionHelper.ExecuteChain(context, first, []);

        Assert.Equal(["first", "second", "third"], calls);
        Assert.Equal(1, context.GetExecutionCount("first"));
        Assert.Equal(1, context.GetExecutionCount("second"));
        Assert.Equal(1, context.GetExecutionCount("third"));
    }

    /// <summary>
    /// 조건 분기 내부와 그 후속 노드의 실행 횟수가 통계에 모두 반영되는지 확인합니다.
    /// </summary>
    [Fact]
    public void NestedBranchExecutionsAreIncludedInStats()
    {
        var tracker = new NodeStatsTracker();
        var originalTracker = ActionNodeBase.GetStatsTracker();
        ActionNodeBase.SetStatsTracker(tracker);
        try
        {
            var calls = new List<string>();
            var branch = Count("branch", calls);
            branch.NextNodes.Add(Count("branch-next", calls));
            var conditional = new ExposedConditionalNode
            {
                Name = "conditional",
                Condition = _ => true,
                TrueNodes = [branch]
            };

            conditional.ExecuteContext(new RuntimeContext(null!, null));

            Assert.Equal(["branch", "branch-next"], calls);
            Assert.Equal(
                1,
                Assert.IsType<NodeExecutionStats>(tracker.GetStats("branch")).ExecutionCount);
            Assert.Equal(
                1,
                Assert.IsType<NodeExecutionStats>(tracker.GetStats("branch-next")).ExecutionCount);
        }
        finally
        {
            ActionNodeBase.SetStatsTracker(originalTracker);
        }
    }

    /// <summary>
    /// 특수 노드가 자신을 직접 가리키는 순환 경로를 한 번만 실행하는지 확인합니다.
    /// </summary>
    [Fact]
    public void SpecialNodeDirectCycleStopsAtActiveExecutionPath()
    {
        var node = new ExposedConditionalNode
        {
            Name = "conditional",
            Condition = _ => true
        };
        node.NextNodes.Add(node);
        var context = new RuntimeContext(null!, null);

        NodeExecutionHelper.ExecuteChain(context, node, []);

        Assert.Equal(1, context.GetExecutionCount("conditional"));
    }

    /// <summary>
    /// 특수 노드의 간접 순환 경로가 활성 실행 경로에서 중단되는지 확인합니다.
    /// </summary>
    [Fact]
    public void SpecialNodeIndirectCycleStopsAtActiveExecutionPath()
    {
        var calls = new List<string>();
        var node = new ExposedConditionalNode
        {
            Name = "conditional",
            Condition = _ => true
        };
        var middle = Count("middle", calls);
        node.NextNodes.Add(middle);
        middle.NextNodes.Add(node);
        var context = new RuntimeContext(null!, null);

        NodeExecutionHelper.ExecuteChain(context, node, []);

        Assert.Equal(["middle"], calls);
        Assert.Equal(1, context.GetExecutionCount("conditional"));
        Assert.Equal(1, context.GetExecutionCount("middle"));
    }

    /// <summary>
    /// 동시 패킷 수신 시 각 완료 경로에 대응하는 버퍼와 실행 컨텍스트가 전달되는지 확인합니다.
    /// </summary>
    [Fact]
    public async Task WaitForPacketDispatchPassesEachCompletedBufferAcrossConcurrentReceives()
    {
        var context = new RuntimeContext(null!, null);
        var firstBuffer = new NetBuffer(8);
        var secondBuffer = new NetBuffer(8);
        NetBuffer? firstObserved = null;
        NetBuffer? secondObserved = null;
        NetBuffer? firstContextObserved = null;
        NetBuffer? secondContextObserved = null;
        using var rendezvous = new Barrier(2);
        var firstWait = new WaitForPacketNode
        {
            NextNodes =
            [
                new CustomActionNode
                {
                    ActionHandler = (_, buffer) =>
                    {
                        rendezvous.SignalAndWait();
                        firstObserved = buffer;
                        firstContextObserved = context.GetPacket();
                    }
                }
            ]
        };
        var secondWait = new WaitForPacketNode
        {
            NextNodes =
            [
                new CustomActionNode
                {
                    ActionHandler = (_, buffer) =>
                    {
                        rendezvous.SignalAndWait();
                        secondObserved = buffer;
                        secondContextObserved = context.GetPacket();
                    }
                }
            ]
        };

        await Task.WhenAll(
            Task.Run(() => firstWait.DispatchReceivedPacket(context, firstBuffer)),
            Task.Run(() => secondWait.DispatchReceivedPacket(context, secondBuffer)));

        Assert.Same(firstBuffer, firstObserved);
        Assert.Same(secondBuffer, secondObserved);
        Assert.Same(firstBuffer, firstContextObserved);
        Assert.Same(secondBuffer, secondContextObserved);
    }

    /// <summary>
    /// 조건식이 선택한 분기와 공통 후속 노드를 올바른 순서로 실행하는지 확인합니다.
    /// </summary>
    [Theory]
    [InlineData(true, "true")]
    [InlineData(false, "false")]
    public void ConditionalExecutesSelectedBranchThenCommonNext(bool condition, string expectedBranch)
    {
        var calls = new List<string>();
        var selected = Count(expectedBranch, calls);
        selected.NextNodes.Add(Count($"{expectedBranch}-child", calls));
        var node = new ExposedConditionalNode
        {
            Condition = _ => condition,
            TrueNodes = condition ? [selected] : [Count("unused", calls)],
            FalseNodes = condition ? [Count("unused", calls)] : [selected],
            NextNodes = [Count("next", calls)]
        };

        node.ExecuteContext(new RuntimeContext(null!, null));

        Assert.Equal([expectedBranch, $"{expectedBranch}-child", "next"], calls);
    }

    /// <summary>
    /// Assert 노드가 성공, 계속 진행 실패 및 중단 실패 계약을 각각 준수하는지 확인합니다.
    /// </summary>
    [Fact]
    public void AssertDispatchesPassFailureAndStopContracts()
    {
        var calls = new List<string>();
        var context = new RuntimeContext(null!, null);
        var passing = new ExposedAssertNode
        {
            Condition = _ => true,
            NextNodes = [Count("pass-next", calls)],
            FailureNodes = [Count("unused", calls)]
        };
        passing.ExecuteContext(context);

        var continuing = new ExposedAssertNode
        {
            Condition = _ => false,
            StopOnFailure = false,
            FailureNodes = [Count("failure", calls)],
            NextNodes = [Count("continue", calls)]
        };
        continuing.ExecuteContext(context);

        var stopping = new ExposedAssertNode
        {
            Condition = _ => false,
            StopOnFailure = true,
            ErrorMessage = "stop",
            FailureNodes = [Count("stop-failure", calls)],
            NextNodes = [Count("must-not-run", calls)]
        };

        var exception = Assert.Throws<AssertionFailedException>(() => stopping.ExecuteContext(context));
        Assert.Equal("stop", exception.Message);
        Assert.Equal(["pass-next", "failure", "continue", "stop-failure"], calls);
    }

    /// <summary>
    /// 조건 평가 예외를 실패 경로로 보내고 설정에 따라 후속 실행을 계속하는지 확인합니다.
    /// </summary>
    [Fact]
    public void AssertConditionExceptionUsesFailureAndContinuesWhenConfigured()
    {
        var calls = new List<string>();
        var node = new ExposedAssertNode
        {
            Condition = _ => throw new InvalidOperationException("condition"),
            StopOnFailure = false,
            FailureNodes = [Count("failure", calls)],
            NextNodes = [Count("next", calls)]
        };

        node.ExecuteContext(new RuntimeContext(null!, null));

        Assert.Equal(["failure", "next"], calls);
    }

    /// <summary>
    /// 변수 설정 노드가 지원하는 모든 형식을 문화권에 독립적으로 변환하는지 확인합니다.
    /// </summary>
    [Fact]
    public void SetVariableConvertsEverySupportedTypeUsingInvariantCulture()
    {
        var context = new RuntimeContext(null!, null);

        Set(context, "int", "i", "-12");
        Set(context, "long", "l", "9223372036854775806");
        Set(context, "float", "f", "1.25");
        Set(context, "double", "d", "-2.5");
        Set(context, "bool", "b", "true");
        Set(context, "string", "s", "hello");

        Assert.Equal(-12, context.Get<int>("i"));
        Assert.Equal(9223372036854775806L, context.Get<long>("l"));
        Assert.Equal(1.25f, context.Get<float>("f"));
        Assert.Equal(-2.5, context.Get<double>("d"));
        Assert.True(context.Get<bool>("b"));
        Assert.Equal("hello", context.Get<string>("s"));
    }

    /// <summary>
    /// 변수 값 파싱에 실패했을 때 기존 값을 덮어쓰지 않는지 확인합니다.
    /// </summary>
    [Fact]
    public void SetVariableParseFailureDoesNotOverwriteExistingValue()
    {
        var context = new RuntimeContext(null!, null);
        context.Set("value", 7);

        Set(context, "int", "value", "not-an-int");

        Assert.Equal(7, context.Get<int>("value"));
    }

    /// <summary>
    /// 가중치로 선택된 분기와 공통 후속 노드를 정확히 한 번씩 실행하는지 확인합니다.
    /// </summary>
    [Fact]
    public void RandomChoiceTraversesSelectedBranchAndCommonNextExactlyOnce()
    {
        var calls = new List<string>();
        var selected = Count("selected", calls);
        selected.NextNodes.Add(Count("selected-child", calls));
        var node = new RandomChoiceNode
        {
            Choices =
            [
                new ChoiceOption { Name = "first", Weight = 2, Node = Count("first", calls) },
                new ChoiceOption { Name = "second", Weight = 3, Node = selected }
            ],
            NextNodes = [Count("next", calls)]
        };

        node.ExecuteImpl(new RuntimeContext(null!, null), _ => 2);

        Assert.True(NodeExecutionHelper.HandlesOwnNextNode(node));
        Assert.Equal(["selected", "selected-child", "next"], calls);
    }

    /// <summary>
    /// 0 이하의 선택 가중치를 거부하고 어떤 노드도 실행하지 않는지 확인합니다.
    /// </summary>
    [Theory]
    [InlineData(0)]
    [InlineData(-1)]
    public void RandomChoiceRejectsNonPositiveWeightsWithoutExecuting(int invalidWeight)
    {
        var calls = new List<string>();
        var node = new RandomChoiceNode
        {
            Choices =
            [
                new ChoiceOption { Weight = invalidWeight, Node = Count("choice", calls) }
            ],
            NextNodes = [Count("next", calls)]
        };

        node.ExecuteImpl(new RuntimeContext(null!, null), _ => 0);

        Assert.Empty(calls);
    }

    /// <summary>
    /// 전체 선택 가중치가 int 최댓값을 넘어도 오버플로 없이 분기를 선택하는지 확인합니다.
    /// </summary>
    [Fact]
    public void RandomChoiceSupportsPositiveWeightTotalsBeyondIntMaximum()
    {
        var calls = new List<string>();
        var node = new RandomChoiceNode
        {
            Choices =
            [
                new ChoiceOption { Weight = int.MaxValue, Node = Count("first", calls) },
                new ChoiceOption { Weight = 1, Node = Count("second", calls) }
            ]
        };

        node.ExecuteImpl(
            new RuntimeContext(null!, null),
            maximum =>
            {
                Assert.Equal((long)int.MaxValue + 1, maximum);
                return int.MaxValue;
            });

        Assert.Equal(["second"], calls);
    }

    /// <summary>
    /// int 최댓값을 포함하는 지연 범위를 오버플로 없이 선택하는지 확인합니다.
    /// </summary>
    [Fact]
    public void RandomDelaySupportsInclusiveIntMaximumWithoutOverflow()
    {
        var node = new RandomDelayNode
        {
            MinDelayMilliseconds = int.MaxValue,
            MaxDelayMilliseconds = int.MaxValue
        };

        var selected = node.SelectDelay((minimum, exclusiveMaximum) =>
        {
            Assert.Equal(int.MaxValue, minimum);
            Assert.Equal((long)int.MaxValue + 1, exclusiveMaximum);
            return minimum;
        });

        Assert.Equal(int.MaxValue, selected);
    }

    /// <summary>
    /// 사용자 정의 액션에 인자가 전달되고 액션 예외가 호출자에게 전파되는지 확인합니다.
    /// </summary>
    [Fact]
    public void CustomActionReceivesArgumentsAndPropagatesExceptions()
    {
        Client? capturedClient = null;
        NetBuffer? capturedBuffer = null;
        var buffer = new NetBuffer(8);
        var action = new CustomActionNode
        {
            ActionHandler = (client, packet) =>
            {
                capturedClient = client;
                capturedBuffer = packet;
            }
        };

        action.Execute(null!, buffer);

        Assert.Null(capturedClient);
        Assert.Same(buffer, capturedBuffer);

        action.ActionHandler = (_, _) => throw new InvalidOperationException("failure");
        Assert.Throws<InvalidOperationException>(() => action.Execute(null!, buffer));
    }

    private static CountingNode Count(string name, List<string> calls) =>
        new(name, calls) { Name = name };

    private static void Set(RuntimeContext context, string type, string name, string value)
    {
        new ExposedSetVariableNode
        {
            ValueType = type,
            VariableName = name,
            StringValue = value
        }.ExecuteContext(context);
    }

    private sealed class CountingNode(string name, List<string> calls) : ActionNodeBase
    {
        public override void Execute(Client client, NetBuffer? receivedPacket = null)
        {
            calls.Add(name);
        }
    }

    private sealed class ExposedConditionalNode : ConditionalNode
    {
        public void ExecuteContext(RuntimeContext context) => base.ExecuteImpl(context);
    }

    private sealed class ExposedAssertNode : AssertNode
    {
        public void ExecuteContext(RuntimeContext context) => base.ExecuteImpl(context);
    }

    private sealed class ExposedSetVariableNode : SetVariableNode
    {
        public void ExecuteContext(RuntimeContext context) => base.ExecuteImpl(context);
    }
}
