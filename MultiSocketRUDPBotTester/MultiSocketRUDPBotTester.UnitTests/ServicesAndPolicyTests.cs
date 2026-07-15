using System.Text.Json;
using MultiSocketRUDPBotTester.AI;
using MultiSocketRUDPBotTester.Bot;
using MultiSocketRUDPBotTester.ClientCore;
using MultiSocketRUDPBotTester.Evaluation;

namespace MultiSocketRUDPBotTester.UnitTests;

public sealed class AiTreeServiceTests
{
    private readonly AiTreeService service = new();

    /// <summary>
    /// 지원하는 AI 응답 형태에서 최상위 JSON 객체를 추출하고 파싱하는지 확인합니다.
    /// </summary>
    [Theory]
    [InlineData("{\"type\":\"LogNode\"}")]
    [InlineData("```json\n{\"type\":\"LogNode\"}\n```")]
    [InlineData("prefix {\"type\":\"LogNode\"} suffix")]
    public void ParseExtractsRootJsonFromSupportedResponseShapes(string responseText)
    {
        var response = service.Parse(responseText);

        Assert.False(response.IsError);
        Assert.Equal("LogNode", response.RootNode.GetProperty("type").GetString());
        Assert.Contains("LogNode", response.TreeJson);
    }

    /// <summary>
    /// tree 속성과 description이 포함된 응답을 구조화된 결과로 변환하는지 확인합니다.
    /// </summary>
    [Fact]
    public void ParseUsesTreePropertyAndDescription()
    {
        var response = service.Parse("""
            {"description":"sample","tree":{"type":"DelayNode"}}
            """);

        Assert.False(response.IsError);
        Assert.Equal("sample", response.Description);
        Assert.Equal("DelayNode", response.RootNode.GetProperty("type").GetString());
    }

    /// <summary>
    /// AI 오류 응답이 오류 코드와 메시지를 보존한 구조화된 결과로 반환되는지 확인합니다.
    /// </summary>
    [Fact]
    public void ParseReturnsStructuredAiError()
    {
        var response = service.Parse("""
            {"error":{"reason":"unsupported","details":"details"}}
            """);

        Assert.True(response.IsError);
        Assert.Equal("unsupported", response.ErrorReason);
        Assert.Equal("details", response.ErrorDetails);
    }

    /// <summary>
    /// JSON이 없거나 형식이 잘못된 응답을 파싱 실패로 거부하는지 확인합니다.
    /// </summary>
    [Theory]
    [InlineData("")]
    [InlineData("no json here")]
    [InlineData("{broken}")]
    public void ParseRejectsMissingOrMalformedJson(string text)
    {
        var response = service.Parse(text);

        Assert.True(response.IsError);
        Assert.NotEmpty(response.ErrorReason);
    }

    /// <summary>
    /// 잘못된 응답의 상세 정보가 로그 및 오류 결과에서 제한 길이로 잘리는지 확인합니다.
    /// </summary>
    [Fact]
    public void ParseTruncatesInvalidResponseDetails()
    {
        var response = service.Parse(new string('x', 700));

        Assert.True(response.IsError);
        Assert.True(response.ErrorDetails.Length < 600);
    }

    /// <summary>
    /// validator가 지원하는 모든 자식 속성을 재귀적으로 방문하여 하위 노드를 검사하는지 확인합니다.
    /// </summary>
    [Theory]
    [InlineData("next")]
    [InlineData("true_branch")]
    [InlineData("false_branch")]
    [InlineData("loop_body")]
    [InlineData("repeat_body")]
    [InlineData("timeout_nodes")]
    public void ValidateRecursesThroughEverySupportedChildProperty(string childProperty)
    {
        var response = service.Parse(
            $"{{\"type\":\"LogNode\",\"{childProperty}\":{{\"type\":\"UnknownNode\"}}}}");

        var result = service.Validate(response);

        Assert.False(result.IsValid);
        Assert.Contains(result.Errors, error => error.Contains($"root.{childProperty}") && error.Contains("UnknownNode"));
    }

    /// <summary>
    /// 잘못된 노드 JSON 형태별로 기대한 validation 메시지를 보고하는지 확인합니다.
    /// </summary>
    [Theory]
    [InlineData("{}", "missing 'type'")]
    [InlineData("{\"type\":\"\"}", "empty type")]
    [InlineData("{\"type\":\"Unknown\"}", "Unknown node type")]
    public void ValidateReportsInvalidNodeShape(string json, string expectedMessage)
    {
        var result = service.Validate(service.Parse(json));

        Assert.False(result.IsValid);
        Assert.Contains(result.Errors, error => error.Contains(expectedMessage));
    }

    /// <summary>
    /// 알려진 중첩 트리를 허용하고 JSON formatting 실패 시 원본 문자열을 보존하는지 확인합니다.
    /// </summary>
    [Fact]
    public void ValidateAcceptsKnownNestedTreeAndFormatJsonPreservesInvalidInput()
    {
        var result = service.Validate(service.Parse("""
            {"type":"ConditionalNode","true_branch":{"type":"LogNode"},"false_branch":{"type":"DelayNode"}}
            """));

        Assert.True(result.IsValid);
        Assert.Empty(result.Errors);
        Assert.Contains(Environment.NewLine, service.FormatJson("{\"a\":1}"));
        Assert.Equal("not-json", service.FormatJson("not-json"));
    }
}

public sealed class ConditionEvaluatorTests
{
    private readonly RuntimeContext context = new(null!, null);

    /// <summary>
    /// ConditionEvaluator가 invariant culture 숫자와 비교 연산자를 올바르게 평가하는지 확인합니다.
    /// </summary>
    [Theory]
    [InlineData("2", ">", "1", true)]
    [InlineData("2", "<", "1", false)]
    [InlineData("2", ">=", "2", true)]
    [InlineData("2", "<=", "2", true)]
    [InlineData("1", "==", "1.00005", true)]
    [InlineData("1", "!=", "1.00005", false)]
    [InlineData("1", "==", "1.0002", false)]
    [InlineData("1,000.5", ">", "999", true)]
    public void EvaluateComparesInvariantNumbers(string left, string op, string right, bool expected)
    {
        Assert.Equal(expected, ConditionEvaluator.Evaluate(context, null, left, op, null, right));
    }

    /// <summary>
    /// boolean·문자열 비교를 처리하고 지원하지 않는 연산자는 false를 반환하는지 확인합니다.
    /// </summary>
    [Theory]
    [InlineData("true", "==", "true", true)]
    [InlineData("true", "!=", "false", true)]
    [InlineData("alpha", "==", "alpha", true)]
    [InlineData("alpha", "!=", "beta", true)]
    [InlineData("alpha", ">", "beta", false)]
    [InlineData("1", "unsupported", "1", false)]
    public void EvaluateHandlesBooleanStringAndUnsupportedOperators(string left, string op, string right, bool expected)
    {
        Assert.Equal(expected, ConditionEvaluator.Evaluate(context, null, left, op, null, right));
    }

    /// <summary>
    /// 존재하지 않는 getter를 참조한 조건 평가가 예외 대신 false를 반환하는지 확인합니다.
    /// </summary>
    [Fact]
    public void EvaluateReturnsFalseWhenGetterDoesNotExist()
    {
        Assert.False(ConditionEvaluator.Evaluate(
            context, "Getter Function", "MissingGetter", "==", null, "0"));
    }
}

public sealed class PacketLossSimulatorTests
{
    /// <summary>
    /// 비활성화된 패킷 손실 simulator가 송신과 수신 패킷을 항상 통과시키는지 확인합니다.
    /// </summary>
    [Fact]
    public void DisabledSimulatorNeverDrops()
    {
        var simulator = new PacketLossSimulator(1.0, 7);

        Assert.All(Enumerable.Range(0, 100), _ =>
        {
            Assert.False(simulator.ShouldDropReceivedDatagram());
            Assert.False(simulator.ShouldDropSendingDatagram());
        });
    }

    /// <summary>
    /// 손실률 0과 1의 경계에서 손실 판정이 결정적인 결과를 반환하는지 확인합니다.
    /// </summary>
    [Theory]
    [InlineData(0.0, false)]
    [InlineData(1.0, true)]
    public void EnabledBoundaryRatesAreDeterministic(double rate, bool expectedDrop)
    {
        var simulator = new PacketLossSimulator(rate, 13);
        simulator.SetEnabled(true);

        Assert.All(Enumerable.Range(0, 1_000), _ =>
        {
            Assert.Equal(expectedDrop, simulator.ShouldDropReceivedDatagram());
            Assert.Equal(expectedDrop, simulator.ShouldDropSendingDatagram());
        });
    }

    /// <summary>
    /// 동일한 시드가 송수신 스트림을 재현하고 송신 호출이 수신 난수열에 영향을 주지 않는지 확인합니다.
    /// </summary>
    [Fact]
    public void SameSeedReproducesEachStreamAndSendCallsDoNotPerturbReceiveStream()
    {
        var first = Enabled(0.5, 1234);
        var second = Enabled(0.5, 1234);
        var receiveOnly = Enabled(0.5, 1234);

        var firstReceive = new List<bool>();
        var secondReceive = new List<bool>();
        var firstSend = new List<bool>();
        var secondSend = new List<bool>();
        var isolatedReceive = new List<bool>();
        for (var i = 0; i < 128; i++)
        {
            firstSend.Add(first.ShouldDropSendingDatagram());
            firstReceive.Add(first.ShouldDropReceivedDatagram());
            secondSend.Add(second.ShouldDropSendingDatagram());
            secondReceive.Add(second.ShouldDropReceivedDatagram());
            isolatedReceive.Add(receiveOnly.ShouldDropReceivedDatagram());
        }

        Assert.Equal(firstSend, secondSend);
        Assert.Equal(firstReceive, secondReceive);
        Assert.Equal(firstReceive, isolatedReceive);
    }

    /// <summary>
    /// 경계 손실률의 병렬 호출이 완료된 뒤 전체 손실 횟수가 기대값과 일치하는지 확인합니다.
    /// </summary>
    [Theory]
    [InlineData(0.0, 0)]
    [InlineData(1.0, 10_000)]
    public void ConcurrentBoundaryCallsPreserveJoinedDropCount(double rate, int expectedDrops)
    {
        var simulator = Enabled(rate, 9);
        var drops = 0;

        Parallel.For(0, 10_000, _ =>
        {
            if (simulator.ShouldDropReceivedDatagram())
            {
                Interlocked.Increment(ref drops);
            }
        });

        Assert.Equal(expectedDrops, drops);
    }

    private static PacketLossSimulator Enabled(double rate, int seed)
    {
        var simulator = new PacketLossSimulator(rate, seed);
        simulator.SetEnabled(true);
        return simulator;
    }
}

public sealed class PacketSchemaTests
{
    /// <summary>
    /// 기본 PacketSchema가 예상 필드를 노출하고 알 수 없는 PacketId에는 null을 반환하는지 확인합니다.
    /// </summary>
    [Fact]
    public void BuiltInSchemasExposeExpectedFieldsAndUnknownReturnsNull()
    {
        Assert.Empty(Assert.IsType<PacketFieldDef[]>(PacketSchema.Get(PacketId.Ping)));
        var stringSchema = Assert.IsType<PacketFieldDef[]>(PacketSchema.Get(PacketId.TestStringPacketReq));
        var field = Assert.Single(stringSchema);
        Assert.Equal("testString", field.Name);
        Assert.Equal(FieldType.String, field.Type);
        Assert.Equal(string.Empty, field.DefaultValue);
        Assert.Null(PacketSchema.Get((PacketId)0xFFFF0000U));
    }

    /// <summary>
    /// schema 재등록이 기존 정의를 덮어쓰며 테스트 종료 시 전역 schema 상태를 복원하는지 확인합니다.
    /// </summary>
    [Fact]
    public void RegisterOverridesSchemaAndTestRestoresStaticState()
    {
        const PacketId id = PacketId.Ping;
        var original = Assert.IsType<PacketFieldDef[]>(PacketSchema.Get(id));
        var fields = new[]
        {
            new PacketFieldDef { Name = "value", Type = FieldType.Uint, DefaultValue = 3U }
        };

        try
        {
            PacketSchema.Register(id, fields);
            Assert.Same(fields, PacketSchema.Get(id));
        }
        finally
        {
            PacketSchema.Register(id, original);
        }
    }
}
