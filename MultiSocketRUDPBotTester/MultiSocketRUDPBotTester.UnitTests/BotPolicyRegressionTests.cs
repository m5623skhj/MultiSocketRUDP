using MultiSocketRUDPBotTester.AI;
using MultiSocketRUDPBotTester.Bot;
using MultiSocketRUDPBotTester.Buffer;
using MultiSocketRUDPBotTester.Contents.Client;

namespace MultiSocketRUDPBotTester.UnitTests;

public sealed class BotPolicyRegressionTests
{
    [Fact]
    public void PacketCandidatesCombineWildcardAndExactWithoutIncludingOtherIds()
    {
        var graph = new ActionGraph();
        var wildcard = Root("wildcard", packetId: null);
        var exact = Root("exact", PacketId.Ping);
        var other = Root("other", PacketId.Pong);
        graph.AddNode(wildcard);
        graph.AddNode(exact);
        graph.AddNode(other);

        var candidates = graph.ResolveCandidates(TriggerType.OnPacketReceived, PacketId.Ping);

        Assert.Equal(2, candidates.Count);
        Assert.Contains(wildcard, candidates);
        Assert.Contains(exact, candidates);
        Assert.DoesNotContain(other, candidates);
        Assert.Equal(candidates.Count, candidates.Distinct().Count());
    }

    [Theory]
    [InlineData("exit_nodes")]
    [InlineData("retry_body")]
    [InlineData("failure_nodes")]
    public void AiValidationRecursesThroughEveryFactoryChildPath(string childProperty)
    {
        var service = new AiTreeService();
        var response = service.Parse(
            $"{{\"type\":\"RetryNode\",\"{childProperty}\":{{\"type\":\"UnknownNode\"}}}}");

        var result = service.Validate(response);

        Assert.False(result.IsValid);
        Assert.Contains(
            result.Errors,
            error => error.Contains($"root.{childProperty}") && error.Contains("UnknownNode"));
    }

    [Fact]
    public void ValidatorAcceptsSchemaBackedSendAndRejectsMissingBuilderAndSchema()
    {
        var valid = Root("valid");
        valid.PacketId = PacketId.TestPacketReq;
        var invalid = Root("invalid");
        invalid.PacketId = (PacketId)999;
        var graph = new ActionGraph();
        graph.AddNode(valid);
        graph.AddNode(invalid);

        var result = GraphValidator.ValidateGraph(graph);

        Assert.DoesNotContain(
            result.Issues,
            issue => issue.NodeName == "valid" && issue.Severity == ValidationSeverity.Error);
        Assert.Contains(
            result.Issues,
            issue => issue.NodeName == "invalid"
                && issue.Message.Contains("PacketBuilder or packet schema"));
    }

    [Fact]
    public void ValidatorReportsControlAndVariableBoundaryConfigurations()
    {
        var nodes = new ActionNodeBase[]
        {
            new RandomDelayNode { Name = "random-negative", MinDelayMilliseconds = -1 },
            new RandomDelayNode { Name = "random-inverted", MinDelayMilliseconds = 5, MaxDelayMilliseconds = 4 },
            new RepeatTimerNode { Name = "repeat-count", RepeatCount = 0 },
            new RepeatTimerNode { Name = "repeat-interval", RepeatCount = 1, IntervalMilliseconds = -1 },
            new LoopNode { Name = "loop-zero", ContinueCondition = _ => true, MaxIterations = 0 },
            new RetryNode { Name = "retry-count", MaxRetries = 0 },
            new RetryNode { Name = "retry-delay", MaxRetries = 1, RetryDelayMilliseconds = -1 },
            new WaitForPacketNode { Name = "wait-timeout", ExpectedPacketId = PacketId.Ping, TimeoutMilliseconds = 0 },
            new RandomChoiceNode
            {
                Name = "choice-null",
                Choices =
                [
                    new ChoiceOption { Weight = 1 },
                    new ChoiceOption { Weight = 1, Node = new PolicyNode() }
                ]
            },
            new RandomChoiceNode
            {
                Name = "choice-weight",
                Choices =
                [
                    new ChoiceOption { Weight = 0, Node = new PolicyNode() },
                    new ChoiceOption { Weight = 1, Node = new PolicyNode() }
                ]
            },
            new SetVariableNode { Name = "variable-name", VariableName = "", ValueType = "int", StringValue = "1" },
            new SetVariableNode { Name = "variable-type", VariableName = "v", ValueType = "decimal", StringValue = "1" },
            new SetVariableNode { Name = "variable-value", VariableName = "v", ValueType = "int", StringValue = "bad" }
        };
        foreach (var node in nodes)
        {
            node.Trigger = new TriggerCondition { Type = TriggerType.Manual };
        }

        var graph = new ActionGraph();
        foreach (var node in nodes)
        {
            graph.AddNode(node);
        }

        var result = GraphValidator.ValidateGraph(graph);

        foreach (var node in nodes)
        {
            Assert.Contains(
                result.Issues,
                issue => issue.NodeName == node.Name && issue.Severity == ValidationSeverity.Error);
        }
    }

    [Fact]
    public void SchemaSerializationWritesDefaultsAndOverridesInDeclaredOrder()
    {
        const PacketId packetId = PacketId.Ping;
        var original = Assert.IsType<PacketFieldDef[]>(PacketSchema.Get(packetId));
        try
        {
            PacketSchema.Register(packetId,
            [
                new PacketFieldDef { Name = "byte", Type = FieldType.Byte, DefaultValue = (byte)1 },
                new PacketFieldDef { Name = "ushort", Type = FieldType.Ushort, DefaultValue = (ushort)2 },
                new PacketFieldDef { Name = "int", Type = FieldType.Int, DefaultValue = -3 },
                new PacketFieldDef { Name = "uint", Type = FieldType.Uint, DefaultValue = 4U },
                new PacketFieldDef { Name = "ulong", Type = FieldType.Ulong, DefaultValue = 5UL },
                new PacketFieldDef { Name = "string", Type = FieldType.String, DefaultValue = "default" }
            ]);
            var node = new SendPacketNode
            {
                PacketId = packetId,
                FieldValues =
                {
                    ["ushort"] = 42,
                    ["string"] = "override"
                }
            };

            var buffer = Assert.IsType<NetBuffer>(node.BuildFromSchema());
            buffer.SkipBytes(5);

            Assert.Equal(1, buffer.ReadByte());
            Assert.Equal(42, buffer.ReadUShort());
            Assert.Equal(-3, buffer.ReadInt());
            Assert.Equal(4U, buffer.ReadUInt());
            Assert.Equal(5UL, buffer.ReadULong());
            Assert.Equal("override", buffer.ReadString());
        }
        finally
        {
            PacketSchema.Register(packetId, original);
        }
    }

    [Fact]
    public void PendingTaskExchangeReturnsExactlyOneRegisteredTask()
    {
        var context = new RuntimeContext(null!, null);
        var marker = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        context.SetPendingAsyncTask(marker.Task);
        var returned = new Task[64];

        Parallel.For(0, returned.Length, i => returned[i] = context.GetAndClearPendingAsyncTask());

        Assert.Single(returned, task => ReferenceEquals(task, marker.Task));
        Assert.Equal(returned.Length - 1, returned.Count(task => task.IsCompletedSuccessfully));
        Assert.Throws<ArgumentNullException>(() => context.SetPendingAsyncTask(null!));
    }

    private static SendPacketNode Root(string name, PacketId? packetId = PacketId.Ping) => new()
    {
        Name = name,
        PacketId = packetId ?? PacketId.Ping,
        Trigger = new TriggerCondition
        {
            Type = TriggerType.OnPacketReceived,
            PacketId = packetId
        }
    };

    private sealed class PolicyNode : ActionNodeBase
    {
        public override void Execute(Client client, NetBuffer? receivedPacket = null)
        {
        }
    }
}
