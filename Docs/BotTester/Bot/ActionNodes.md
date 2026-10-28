# 노드 타입 레퍼런스 (Action Nodes)

> [[BotActionGraph]]을 구성하는 노드 타입의 동작, 설정값과 AI JSON 가져오기 범위를 정리한다. 기본값은 별도 표기가 없으면 런타임 노드 속성 기준이다.

이 문서의 JSON은 [[AiTreeGenerator]]가 읽는 트리 형식이다. 캔버스 저장 파일 형식은 [[GraphFileStorage]]를 참고한다. 코드에서 설정할 수 있는 모든 속성을 AI JSON으로 가져올 수 있는 것은 아니며, 지원 제한은 각 노드에 명시한다.

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

`RandomChoiceNode`는 Action 카테고리지만 `choice_0`, `choice_1` 등의 동적 선택 포트도 사용한다.

---

## 비동기 노드

다음 노드들은 다음 분기의 실행을 직접 관리하므로 공통 실행기가 `NextNodes`를 자동 순회하지 않는다. 이 분류의 모든 노드가 별도 스레드에서 비동기로 실행된다는 뜻은 아니다:

`DelayNode` · `RandomDelayNode` · `RandomChoiceNode` · `RepeatTimerNode` · `WaitForPacketNode` · `RetryNode` · `ConditionalNode` · `LoopNode` · `AssertNode`

---

## 공통 실행 계약

### `ActionNodeBase`

#### `AddNext(ActionNodeBase nextNode)`
- 기본 체인의 다음 노드를 연결한다.

#### `SetStatsTracker(NodeStatsTracker? tracker)`
- 전역 통계 추적기를 설정한다.

#### `GetStatsTracker()`
- 현재 등록된 통계 추적기를 반환한다.

### 노드 구현 파일 구성

- `SimpleActionNode.cs`: 송신, 지연, 로그, 연결 해제, 사용자 정의 동작 노드
- `ConditionalAndLoopNodes.cs`: 조건 분기와 루프 구조 노드들
- `FlowControlNodes.cs`: 검증과 재시도 노드
- `VariableNodes.cs`: 변수 설정/조회와 패킷 수신 대기 노드
- `PacketParserNode.cs`, `RandomChoiceNode.cs`: 패킷 파서와 가중치 선택 노드

---

## Action 노드 (DimGray)

### SendPacketNode
서버로 패킷을 전송한다.

| 설정 | 타입 | 설명 |
|------|------|------|
| `PacketId` | `PacketId` enum | 전송할 패킷 종류 |
| `PacketBuilder` | `Func<Client, NetBuffer>?` | 설정하면 사용하고, 없으면 `PacketSchema`와 `FieldValues`로 본문 생성 |
| `FieldValues` | `Dictionary<string, object>` | 스키마 필드별 값. 생략한 필드는 스키마 기본값 사용 |
| `Unreliable` | `bool` | 기본 `false`. `true`면 비신뢰성 송신 |

```json
{ "type": "SendPacketNode", "packet_id": "Ping" }
```

AI 가져오기는 `packet_id`만 설정한다. 필드값과 `Unreliable`은 가져온 뒤 UI에서 설정하거나 코드로 지정한다.

---

### DelayNode
지정된 시간(ms)만큼 대기 후 다음 노드를 실행한다.

| 설정 | 타입 | 기본값 |
|------|------|--------|
| `DelayMilliseconds` | `int` | 0. 그래프 검증을 통과하려면 양수로 설정 |

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
{ "type": "LogNode", "message": "세션 {sessionId} 연결됨" }
```

---

### DisconnectNode
클라이언트를 서버로부터 정상 종료한다. **반드시 리프 노드**여야 한다.

```json
{ "type": "DisconnectNode", "reason": "테스트 완료" }
```

---

### CustomActionNode
코드에서 직접 `ActionHandler`를 설정해 임의 동작을 수행한다. UI 빌더는 사용자 동작 대신 실행 사실을 로그로 남기는 핸들러를 설정한다. AI JSON은 사용자 코드를 가져오지 않는다.

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

```csharp
// 아래 BotVariables 확장 예시의 Setter를 등록한 뒤 사용한다.
var parser = new PacketParserNode { SetterMethodName = "SetPlayerMoney" };
```

`BotVariables` 클래스에 `[BotVariable]` 어트리뷰트로 등록된 Setter를 사용한다. AI 검증기는 이 노드 타입을 허용하지만 `AiNodeFactory`는 `setter_method`를 읽지 않는다. 가져온 뒤 UI에서 Setter를 지정해야 빌드할 수 있다.

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
  "condition": "true",
  "true_branch": { "type": "LogNode", "message": "조건 참" },
  "false_branch": { "type": "LogNode", "message": "조건 거짓" }
}
```

**UI 설정:** Left / Operator / Right 3단 구성. Left 또는 Right에 `Getter Function`을 선택하면 `BotVariables` Getter 목록에서 선택 가능.

AI 가져오기는 `condition` 문자열을 상수인 Left에 넣고 상수 `true`와 `==`로 비교한다. 위 예제는 항상 참이다. `Context.Get(...) > 100` 같은 식을 해석하는 기능은 없으므로 동적 비교는 UI에서 Getter와 비교 연산자를 설정하거나 코드의 `Condition` 델리게이트로 작성한다.

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
  "max_iterations": 10,
  "loop_body": { "type": "LogNode", "message": "반복 실행" },
  "exit_nodes": { "type": "LogNode", "message": "반복 종료" }
}
```

AI 가져오기와 UI 빌더는 `max_iterations`를 `LoopCount`로 매핑해 횟수 기반 조건을 만든다. JSON의 자유 형식 `condition`은 읽지 않는다. 코드에서 노드를 직접 구성할 때만 임의의 `ContinueCondition`을 지정할 수 있다.

---

### RepeatTimerNode
정해진 횟수만큼 `IntervalMilliseconds` 간격으로 `RepeatBody`를 반복.
비동기(`Task.Run`)로 실행되며 `CancellationToken` 지원.

| 설정 | 기본값 |
|------|--------|
| `RepeatCount` | 10 |
| `IntervalMilliseconds` | 1000 |

```json
{ "type": "RepeatTimerNode", "repeat_count": 5, "interval_ms": 2000, "repeat_body": { "type": "LogNode", "message": "주기 실행" } }
```

---

### WaitForPacketNode
`ExpectedPacketId`의 패킷이 수신될 때까지 대기. 타임아웃 시 `TimeoutNodes` 실행.

`Client.WaitForNextPacketAsync()`의 대기 등록과 완료 통지를 사용한다. 성공한 수신 버퍼를 다음 노드에 전달하며 예약 키 폴링은 사용하지 않는다.

| 설정 | 기본값 |
|------|--------|
| `ExpectedPacketId` | InvalidPacketId |
| `TimeoutMilliseconds` | 5000 |

```json
{ "type": "WaitForPacketNode", "expected_packet_id": "Pong", "timeout_ms": 5000, "timeout_nodes": { "type": "LogNode", "message": "수신 시간 초과" } }
```

---

### AssertNode
조건이 false이면 `FailureNodes`를 실행하고 `StopOnFailure`가 true이면 `AssertionFailedException`을 던진다.

```csharp
var assertion = new AssertNode
{
    Condition = ctx => ctx.GetOrDefault("result", false),
    ErrorMessage = "결과가 참이어야 한다",
    StopOnFailure = true
};
```

UI에서는 Left / Operator / Right로 조건을 지정한다. AI 가져오기는 `error_message`, `stop_on_failure`, `failure_nodes`를 반영하지만 `condition`은 읽지 않는다. 가져온 노드는 UI에서 조건을 다시 설정해야 한다. 미설정 상태를 의도한 검증으로 사용하면 안 된다.

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
  "retry_body": { "type": "LogNode", "message": "재시도 본문" },
  "failure_nodes": { "type": "LogNode", "message": "재시도 실패" }
}
```

위 JSON은 가져오기 구조 예시다. AI 가져오기는 `SuccessCondition`과 별도의 `SuccessNodes`를 설정하지 않으므로 응답 성공 여부를 자동 판정하는 예제로 해석하지 않는다.

---

### RandomChoiceNode
`Choices` 목록에서 가중치 기반으로 무작위 선택 후 해당 노드를 실행.
UI에서 `choice_0`, `choice_1` ... 동적 포트로 연결.

```csharp
var choice = new RandomChoiceNode
{
    Choices =
    [
        new ChoiceOption { Name = "적은 비중", Weight = 1, Node = new DelayNode { DelayMilliseconds = 100 } },
        new ChoiceOption { Name = "큰 비중", Weight = 3, Node = new DelayNode { DelayMilliseconds = 300 } }
    ]
};
```

이 노드는 UI 또는 코드로 구성한다. AI 검증기의 `KnownNodeTypes`에 없고 `AiNodeFactory`도 `choices`를 읽지 않으므로 AI 트리 JSON으로 가져올 수 없다. 그래프 검증을 위해 최소 2개 선택지에 연결 대상과 양수 가중치를 지정한다.

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
- [[PacketSchema]] — 패킷 필드 입력 schema
- [[NodeConfigPanels]] — 노드별 설정 UI
