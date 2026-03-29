# 노드 타입 레퍼런스 (Action Nodes)

> [[BotActionGraph]]을 구성하는 모든 노드 타입의 설명, 설정값, JSON 스키마.

---

## 노드 계층 구조

![[NodeHierarchy.svg]]

---

## 노드 분류

| 카테고리 | 색상 | 포트 구성 |
|---------|------|-----------|
| **Action** | DimGray | 입력 1개 + `default` 출력 1개 |
| **Condition** | DarkOrange | 입력 1개 + `true` / `false` 출력 |
| **Loop** | DarkMagenta | 입력 1개 + `continue` / `exit` 출력 |

---

## 비동기 노드

다음 노드들은 **비동기로 실행**되므로 실행 후 다음 노드로 즉시 반환 (NextNodes를 직접 관리):

`DelayNode` · `RandomDelayNode` · `RepeatTimerNode` · `WaitForPacketNode` · `RetryNode` · `ConditionalNode` · `LoopNode` · `AssertNode`

---

## Action 노드 (DimGray)

### SendPacketNode
서버로 패킷을 전송한다.

| 설정 | 타입 | 설명 |
|------|------|------|
| `PacketId` | `PacketId` enum | 전송할 패킷 종류 |
| `PacketBuilder` | `Func<Client, NetBuffer>` | 패킷 데이터 생성 함수 |

```json
{ "type": "SendPacketNode", "packet_id": "Ping" }
```

---

### DelayNode
지정된 시간(ms)만큼 대기 후 다음 노드를 실행한다.

| 설정 | 타입 | 기본값 |
|------|------|--------|
| `DelayMilliseconds` | `int` | 1000 |

```json
{ "type": "DelayNode", "delay_ms": 1000 }
```

---

### RandomDelayNode
최솟값~최댓값 범위 내 무작위 시간 대기.

| 설정 | 타입 | 기본값 |
|------|------|--------|
| `MinDelayMilliseconds` | `int` | 500 |
| `MaxDelayMilliseconds` | `int` | 2000 |

```json
{ "type": "RandomDelayNode", "min_delay_ms": 500, "max_delay_ms": 2000 }
```

---

### LogNode
메시지를 Serilog Information으로 출력한다.

| 설정 | 타입 | 플레이스홀더 |
|------|------|-------------|
| `MessageBuilder` | `Func<Client, NetBuffer?, string>` | `{sessionId}`, `{isConnected}`, `{packetSize}` |

```json
{ "type": "LogNode", "message": "Session {sessionId} connected" }
```

---

### DisconnectNode
클라이언트를 서버로부터 정상 종료한다. **반드시 리프 노드**여야 한다.

```json
{ "type": "DisconnectNode", "reason": "Test completed" }
```

---

### CustomActionNode
코드에서 직접 `ActionHandler`를 설정해 임의 동작 수행. UI에서 생성 시 no-op.

---

### SetVariableNode
`RuntimeContext`에 변수를 저장한다.

| 설정 | 타입 | 예시 |
|------|------|------|
| `VariableName` | `string` | `"money"` |
| `ValueType` | `string` | `"int"` \| `"long"` \| `"float"` \| `"double"` \| `"bool"` \| `"string"` |
| `StringValue` | `string` | `"100"` |

```json
{ "type": "SetVariableNode", "variable_name": "money", "value_type": "int", "value": "100" }
```

---

### GetVariableNode
컨텍스트의 변수 값을 읽어 로그로 출력한다. (디버깅용)

```json
{ "type": "GetVariableNode", "variable_name": "money" }
```

---

### PacketParserNode
수신 패킷을 역직렬화해 `BotVariables`의 Setter 메서드를 통해 컨텍스트에 저장.

```json
{ "type": "PacketParserNode", "setter_method": "SetPlayerMoney" }
```

> `BotVariables` 클래스에 `[BotVariable]` 어트리뷰트로 등록된 메서드만 사용 가능

---

## Condition 노드 (DarkOrange)

### ConditionalNode
조건 평가 결과에 따라 `TrueNodes` 또는 `FalseNodes` 체인을 실행한다.
평가 후 `NextNodes`도 실행한다.

| 설정 | 타입 | 예시 |
|------|------|------|
| `Condition` | `Func<RuntimeContext, bool>` | `ctx => ctx.Get<int>("money") > 100` |

```json
{
  "type": "ConditionalNode",
  "condition": "Context.Get('money') > 100",
  "true_branch": { ... },
  "false_branch": { ... }
}
```

**UI 설정:** Left / Operator / Right 3단 구성. Left 또는 Right에 `Getter Function`을 선택하면 `BotVariables` Getter 목록에서 선택 가능.

---

## Loop 노드 (DarkMagenta)

### LoopNode
`ContinueCondition`이 true인 동안 `LoopBody`를 반복. 종료 시 `ExitNodes` 실행.

| 설정 | 기본값 | 설명 |
|------|--------|------|
| `ContinueCondition` | - | 반복 계속 조건 |
| `MaxIterations` | 100 | 최대 반복 횟수 (안전장치) |

```json
{
  "type": "LoopNode",
  "condition": "Context.Get('count') < 10",
  "max_iterations": 100,
  "loop_body": { ... },
  "exit_nodes": { ... }
}
```

---

### RepeatTimerNode
정해진 횟수만큼 `IntervalMilliseconds` 간격으로 `RepeatBody`를 반복.
비동기(`Task.Run`)로 실행되며 `CancellationToken` 지원.

| 설정 | 기본값 |
|------|--------|
| `RepeatCount` | 10 |
| `IntervalMilliseconds` | 1000 |

```json
{ "type": "RepeatTimerNode", "repeat_count": 5, "interval_ms": 2000, "repeat_body": { ... } }
```

---

### WaitForPacketNode
`ExpectedPacketId`의 패킷이 수신될 때까지 대기. 타임아웃 시 `TimeoutNodes` 실행.

패킷 수신 여부를 `RuntimeContext` 키 `__received_{PacketId}`로 확인.

| 설정 | 기본값 |
|------|--------|
| `ExpectedPacketId` | InvalidPacketId |
| `TimeoutMilliseconds` | 5000 |

```json
{ "type": "WaitForPacketNode", "expected_packet_id": "Pong", "timeout_ms": 5000, "timeout_nodes": { ... } }
```

---

### AssertNode
조건이 false이면 `FailureNodes`를 실행하고 `StopOnFailure`가 true이면 `AssertionFailedException`을 던진다.

```json
{
  "type": "AssertNode",
  "condition": "Context.Get('result') == true",
  "error_message": "Expected result to be true",
  "stop_on_failure": true,
  "failure_nodes": { ... }
}
```

---

### RetryNode
`RetryBody`를 최대 `MaxRetries`회 시도. 성공 시 `SuccessNodes`, 실패 시 `FailureNodes` 실행.
`UseExponentialBackoff`: `delay × 2^(attempt-1)` 지수 백오프.

```json
{
  "type": "RetryNode",
  "max_retries": 3,
  "retry_delay_ms": 1000,
  "exponential_backoff": false,
  "retry_body": { ... },
  "failure_nodes": { ... }
}
```

---

### RandomChoiceNode
`Choices` 목록에서 가중치 기반으로 무작위 선택 후 해당 노드를 실행.
UI에서 `choice_0`, `choice_1` ... 동적 포트로 연결.

```json
{
  "type": "RandomChoiceNode",
  "choices": [
    { "name": "heavy", "weight": 1, "next": { ... } },
    { "name": "light", "weight": 3, "next": { ... } }
  ]
}
```

---

## BotVariables 확장 방법

`BotVariables` 클래스에 어트리뷰트를 추가하면 UI의 Getter/Setter 선택 목록에 자동 반영:

```csharp
public static class BotVariables
{
    [BotVariable("playerMoney", "플레이어 보유 금액", VariableAccessType.Get)]
    public static int GetPlayerMoney(RuntimeContext ctx)
        => ctx.GetOrDefault("playerMoney", 0);

    [BotVariable("playerMoney", "플레이어 보유 금액 설정", VariableAccessType.Set)]
    public static void SetPlayerMoney(RuntimeContext ctx, NetBuffer? buffer)
    {
        if (buffer != null) ctx.Set("playerMoney", (int)buffer.ReadUInt());
    }
}
```

---

## 관련 문서
- [[BotActionGraph]] — 노드 실행 엔진
- [[RuntimeContext]] — 노드 간 데이터 공유
- [[GraphValidator]] — 노드 설정 검증
- [[AiTreeGenerator]] — AI로 노드 트리 자동 생성
