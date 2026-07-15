using System.Collections.Concurrent;
using MultiSocketRUDPBotTester.Bot;
using MultiSocketRUDPBotTester.Buffer;
using static MultiSocketRUDPBotTester.Bot.NodeExecutionStats;

namespace MultiSocketRUDPBotTester.UnitTests;

public sealed class RuntimeContextTests
{
    /// <summary>
    /// RuntimeContext의 변수, 현재 패킷, pending task 및 Clear가 순차 계약에 맞게 동작하는지 확인합니다.
    /// </summary>
    [Fact]
    public void VariablesPacketPendingTaskAndClearFollowSequentialContract()
    {
        var firstPacket = new NetBuffer(8);
        var secondPacket = new NetBuffer(8);
        var context = new RuntimeContext(null!, firstPacket);
        context.Set("answer", 42);
        context.Set("text", "value");

        Assert.Same(firstPacket, context.GetPacket());
        context.SetPacket(secondPacket);
        Assert.Same(secondPacket, context.GetPacket());
        Assert.True(context.Has("answer"));
        Assert.Equal(42, context.Get<int>("answer"));
        Assert.Throws<KeyNotFoundException>(() => context.Get<int>("missing"));
        Assert.Throws<KeyNotFoundException>(() => context.Get<int>("text"));
        Assert.Equal(7, context.GetOrDefault("missing", 7));
        Assert.True(context.Remove("answer"));
        Assert.False(context.Remove("answer"));

        var pending = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        context.SetPendingAsyncTask(pending.Task);
        Assert.Same(pending.Task, context.GetAndClearPendingAsyncTask());
        Assert.True(context.GetAndClearPendingAsyncTask().IsCompletedSuccessfully);

        context.Clear();
        Assert.Null(context.GetPacket());
        Assert.False(context.Has("text"));
        Assert.True(context.GetAndClearPendingAsyncTask().IsCompletedSuccessfully);
    }

    /// <summary>
    /// AtomicIncrement와 GetOrCreate의 병렬 호출 결과가 완료 후 합산 및 단일 생성 불변식을 유지하는지 확인합니다.
    /// </summary>
    [Fact]
    public void AtomicIncrementAndGetOrCreatePreserveJoinedConcurrencyInvariants()
    {
        var context = new RuntimeContext(null!, null);
        var returnedValues = new ConcurrentBag<object>();

        Parallel.For(0, 1_000, _ => context.AtomicIncrement("count"));
        Parallel.For(0, 100, _ => returnedValues.Add(context.GetOrcreate("shared", () => new object())));

        Assert.Equal(1_000, context.Get<int>("count"));
        Assert.Single(returnedValues.Distinct(ReferenceEqualityComparer.Instance));
    }

    /// <summary>
    /// RuntimeContext 확장 함수가 flag, counter, metric 및 타입 기반 조회를 올바르게 처리하는지 확인합니다.
    /// </summary>
    [Fact]
    public void ExtensionsHandleFlagsCountersMetricsAndTypedTryGet()
    {
        var context = new RuntimeContext(null!, null);
        context.SetFlag("ready");
        Assert.True(context.IsFlagSet("ready"));
        context.ClearFlag("ready");
        Assert.False(context.IsFlagSet("ready"));

        context.ResetCounter("items");
        Assert.Equal(3, context.Increment("items", 3));
        Assert.Equal(3, context.GetCounter("items"));
        context.IncrementExecutionCount("node");
        context.IncrementExecutionCount("node");
        Assert.Equal(2, context.GetExecutionCount("node"));

        Assert.False(context.TryGet<int>("missing", out _));
        context.Set("typed", "text");
        Assert.False(context.TryGet<int>("typed", out _));
        Assert.True(context.TryGet<string>("typed", out var text));
        Assert.Equal("text", text);

        Assert.Equal(0, context.GetAverageMetric("latency"));
        Parallel.For(1, 101, i => context.RecordMetric("latency", i));
        Assert.Equal(50.5, context.GetAverageMetric("latency"));
        Assert.Equal(1, context.GetMinMetric("latency"));
        Assert.Equal(100, context.GetMaxMetric("latency"));
    }
}

public sealed class TriggerConditionTests
{
    /// <summary>
    /// 패킷 조건이 없는 trigger가 자신의 trigger 종류에만 일치하는지 확인합니다.
    /// </summary>
    [Fact]
    public void NonPacketTriggerMatchesOnlyItsType()
    {
        var trigger = new TriggerCondition { Type = TriggerType.OnConnected };

        Assert.True(trigger.Matches(TriggerType.OnConnected));
        Assert.False(trigger.Matches(TriggerType.OnDisconnected));
    }

    /// <summary>
    /// 패킷 trigger가 wildcard, 정확한 PacketId 및 추가 validator 조건을 지원하는지 확인합니다.
    /// </summary>
    [Fact]
    public void PacketTriggerSupportsWildcardExactIdAndValidator()
    {
        var validatorCalls = 0;
        var buffer = new NetBuffer(8);
        var trigger = new TriggerCondition
        {
            Type = TriggerType.OnPacketReceived,
            PacketId = PacketId.Ping,
            PacketValidator = candidate =>
            {
                validatorCalls++;
                return ReferenceEquals(candidate, buffer);
            }
        };

        Assert.False(trigger.Matches(TriggerType.OnConnected, PacketId.Ping, buffer));
        Assert.False(trigger.Matches(TriggerType.OnPacketReceived, PacketId.Pong, buffer));
        Assert.False(trigger.Matches(TriggerType.OnPacketReceived, PacketId.Ping));
        Assert.True(trigger.Matches(TriggerType.OnPacketReceived, PacketId.Ping, buffer));
        Assert.Equal(1, validatorCalls);

        var wildcard = new TriggerCondition { Type = TriggerType.OnPacketReceived };
        Assert.True(wildcard.Matches(TriggerType.OnPacketReceived, PacketId.Pong, buffer));
    }
}

public sealed class NodeExecutionStatsTests
{
    /// <summary>
    /// 실행 통계 tracker가 성공·실패·실행 시간 집계를 누적하고 reset으로 초기화되는지 확인합니다.
    /// </summary>
    [Fact]
    public void TrackerAggregatesSuccessFailureTimingAndReset()
    {
        var tracker = new NodeStatsTracker();
        tracker.RecordExecution("alpha", 10, true);
        tracker.RecordExecution("alpha", 30, false, "failed");
        tracker.RecordExecution("beta", 5, true);

        var alpha = Assert.IsType<NodeExecutionStats>(tracker.GetStats("alpha"));
        Assert.Equal(2, alpha.ExecutionCount);
        Assert.Equal(1, alpha.SuccessCount);
        Assert.Equal(1, alpha.FailureCount);
        Assert.Equal(40, alpha.TotalExecutionTimeMs);
        Assert.Equal(10, alpha.MinExecutionTimeMs);
        Assert.Equal(30, alpha.MaxExecutionTimeMs);
        Assert.Equal(20, alpha.AverageExecutionTimeMs);
        Assert.Equal(50, alpha.SuccessRate);
        Assert.Equal("failed", alpha.LastError);
        Assert.NotEqual(default, alpha.LastExecutionTime);
        Assert.Equal("alpha", tracker.GetAllStats()[0].NodeName);

        tracker.ResetNode("alpha");
        Assert.Null(tracker.GetStats("alpha"));
        tracker.Reset();
        Assert.Empty(tracker.GetAllStats());
    }

    /// <summary>
    /// 여러 스레드의 통계 기록이 완료된 뒤 전체 실행·성공·실패 합계가 보존되는지 확인합니다.
    /// </summary>
    [Fact]
    public void ConcurrentRecordsPreserveJoinedAggregateInvariants()
    {
        var tracker = new NodeStatsTracker();

        Parallel.For(0, 1_000, i => tracker.RecordExecution("node", i % 7, i % 2 == 0));

        var stats = Assert.IsType<NodeExecutionStats>(tracker.GetStats("node"));
        Assert.Equal(1_000, stats.ExecutionCount);
        Assert.Equal(500, stats.SuccessCount);
        Assert.Equal(500, stats.FailureCount);
        Assert.Equal(0, stats.MinExecutionTimeMs);
        Assert.Equal(6, stats.MaxExecutionTimeMs);
        Assert.Equal(Enumerable.Range(0, 1_000).Sum(i => (long)(i % 7)), stats.TotalExecutionTimeMs);
    }
}
