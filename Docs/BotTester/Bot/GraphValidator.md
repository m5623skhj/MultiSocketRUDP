# GraphValidator

> 빌드 전 그래프의 구조적·논리적 오류를 사전에 검출하는 정적 검증기.

---

## 검증 단계

```
GraphValidator.ValidateGraph(graph)
 ├─ ValidateBasicStructure()   ← 노드 수, 트리거 노드 존재 여부
 ├─ ValidateCycles()           ← 순환 참조 감지 (DFS)
 ├─ ValidateNodeConfigurations() ← 노드별 설정값 유효성
 └─ ValidateConnectivity()     ← 도달 불가 노드 감지
```

---

## 심각도 (ValidationSeverity)

| 수준 | 의미 | 빌드 차단 |
|------|------|-----------|
| `Error` | 실행 불가능한 오류 | ✅ |
| `Warning` | 잠재적 문제 (실행은 가능) | ❌ (사용자 확인 필요) |
| `Info` | 참고 정보 | ❌ |

---

## 노드별 검증 규칙

### Error 조건

| 노드 | 규칙 |
|------|------|
| `SendPacketNode` | `PacketId == InvalidPacketId` |
| `SendPacketNode` | `PacketBuilder == null` |
| `DelayNode` | `DelayMilliseconds <= 0` |
| `WaitForPacketNode` | `ExpectedPacketId == InvalidPacketId` |
| `ConditionalNode` | `Condition == null` |
| `LoopNode` | `ContinueCondition == null` |
| `AssertNode` | `Condition == null` |
| `RandomChoiceNode` | `Choices.Count < 2` |
| `Graph` | 노드가 0개 |
| `Cycle` | 순환 참조 발생 (LoopNode/RepeatTimerNode 제외) |

### Warning 조건

| 노드 | 규칙 |
|------|------|
| `DelayNode` | `DelayMilliseconds > 60000` |
| `WaitForPacketNode` | `TimeoutNodes.Count == 0` |
| `AssertNode` | `FailureNodes.Count == 0` |
| `LoopNode` | `MaxIterations > 10000` |
| `Graph` | 트리거 노드 없음 |
| `Graph` | 도달 불가 노드 존재 |

---

## 순환 참조 감지

```
DetectCycle(node, visited, recursionStack, path, depth)
  if depth > 500 → Error (무한 재귀 의심)
  
  foreach nextNode in GetAllNextNodes(node):
    if 미방문 → 재귀
    if recursionStack에 있음:
      LoopNode | RepeatTimerNode → Info (정상 루프)
      그 외 → Error (순환 참조)
```

`GetAllNextNodes`는 `NextNodes` 외에도 분기 노드의 모든 자식을 포함:
`TrueNodes`, `FalseNodes`, `LoopBody`, `ExitNodes`, `RepeatBody`, `TimeoutNodes`, `FailureNodes`, `RetryBody` 등

---

## 도달 가능성 검사

트리거가 있는 노드(루트)에서 DFS로 방문 가능한 모든 노드를 수집.
방문되지 않은 노드 → Warning "Unreachable from any trigger"

---

## UI 연동

`BuildGraph_Click`에서 빌드 성공 후 자동 검증:
- Error 존재 → `ValidationWindowBuilder.ShowDialog()` + 빌드 취소
- Warning 존재 → 사용자 확인 대화상자 (계속/취소)

---

## 함수 설명

### `GraphValidationResult`

#### `AddError(string nodeName, string message, string category = "General")`
- 치명적 검증 문제를 기록한다.

#### `AddWarning(string nodeName, string message, string category = "General")`
- 실행은 가능하지만 위험한 상태를 경고로 기록한다.

#### `AddInfo(string nodeName, string message, string category = "General")`
- 참고 수준 정보를 기록한다.

### `GraphValidator`

#### `ValidateGraph(ActionGraph graph)`
- 그래프 검증의 메인 진입점이다.
- 구조, 순환, 노드 설정, 연결성을 순서대로 검사해 하나의 결과 객체로 합친다.

#### `ValidateBasicStructure(List<ActionNodeBase> nodes, GraphValidationResult result)`
- 빈 그래프 여부와 트리거 노드 존재 여부를 검사한다.

#### `ValidateCycles(List<ActionNodeBase> nodes, GraphValidationResult result)`
- 트리거 루트부터 DFS를 시작해 비정상 순환 참조를 탐지한다.

#### `DetectCycle(...)`
- 재귀 방문 스택을 사용해 현재 경로 내 재방문을 감지한다.
- `LoopNode`와 `RepeatTimerNode`는 의도된 루프로 간주해 info로 기록한다.

#### `GetAllNextNodes(ActionNodeBase node)`
- 노드 타입별로 다음 실행 후보를 모두 수집한다.
- 단순 `NextNodes` 외에도 true/false branch, loop body, retry body 같은 특수 분기를 포함한다.

#### `ValidateNodeConfigurations(List<ActionNodeBase> nodes, GraphValidationResult result)`
- 각 노드의 필수 설정값과 비정상 범위를 검사한다.

#### `ValidateConnectivity(List<ActionNodeBase> nodes, GraphValidationResult result)`
- 어떤 트리거 루트에서도 도달할 수 없는 노드를 찾아 warning으로 기록한다.

#### `CollectReachable(ActionNodeBase node, HashSet<ActionNodeBase> reachable)`
- 루트에서 시작해 도달 가능한 노드를 재귀적으로 수집한다.

---

## 관련 문서
- [[BotActionGraphWindow]] — 빌드·검증 버튼
- [[ActionNodes]] — 각 노드 설정값
