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

## 관련 문서
- [[BotActionGraphWindow]] — 빌드·검증 버튼
- [[ActionNodes]] — 각 노드 설정값
