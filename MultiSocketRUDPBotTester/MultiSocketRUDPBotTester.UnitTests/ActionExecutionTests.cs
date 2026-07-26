using MultiSocketRUDPBotTester.Bot;
using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.Contents.Client;
using static MultiSocketRUDPBotTester.Bot.NodeExecutionStats;

namespace MultiSocketRUDPBotTester.UnitTests;

public sealed class ActionExecutionTests
{
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

    [Fact]
    public void SetVariableParseFailureDoesNotOverwriteExistingValue()
    {
        var context = new RuntimeContext(null!, null);
        context.Set("value", 7);

        Set(context, "int", "value", "not-an-int");

        Assert.Equal(7, context.Get<int>("value"));
    }

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
