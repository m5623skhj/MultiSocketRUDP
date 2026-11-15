# NodeExecutionStats (노드 실행 통계)

> 노드별 실행 횟수, 성공/실패율, 소요 시간 등을 수집하는 통계 시스템.

---

## 구조

```
NodeStatsTracker (BotActionGraphWindow 멤버)
 └─ ConcurrentDictionary<string, NodeExecutionStats>
      ├── NodeName
      ├── ExecutionCount
      ├── SuccessCount / FailureCount
      ├── TotalExecutionTimeMs
      ├── MinExecutionTimeMs / MaxExecutionTimeMs
      ├── LastExecutionTime
      └── LastError
```

---

## 수집 시점

`NodeExecutionHelper.ExecuteChainWithStats`는 노드 진입부터 해당 호출의 동기 실행이 끝날 때까지 측정한다:

```text
Stopwatch.Start()
try:
  ContextNodeBase → ExecuteWithContext(context, buffer)
  그 외 → Execute(client, buffer)
  다음 분기를 직접 관리하지 않는 노드 → NextNodes를 재귀 실행
catch:
  success = false, error 기록 후 예외 재전파
finally:
  Stopwatch.Stop()
  statsTracker.RecordExecution(nodeName, elapsed, success, error?)
  ctx.IncrementExecutionCount(nodeName)
  ctx.RecordMetric("{nodeName}_time", elapsed)
```

동기 후속 체인의 시간은 부모 노드에도 포함된다. 노드별 시간을 더하면 같은 실행 구간을 중복 합산할 수 있으므로 개별 노드 자체의 비용으로 해석하지 않는다.

통계 키는 노드 객체가 아니라 `Name`이다. 이름이 같은 여러 노드의 실행은 같은 항목에 합산된다.

---

## 통계 속성

| 속성 | 설명 |
|------|------|
| `ExecutionCount` | 총 실행 횟수 |
| `SuccessCount` | 성공 횟수 |
| `FailureCount` | 실패 횟수 (예외 발생) |
| `AverageExecutionTimeMs` | 평균 실행 시간 |
| `MinExecutionTimeMs` | 최소 실행 시간 |
| `MaxExecutionTimeMs` | 최대 실행 시간 |
| `SuccessRate` | `SuccessCount / ExecutionCount × 100` (%) |
| `LastError` | 마지막 오류 메시지 |

---

## 통계 창 (StatsWindowBuilder)

`View Statistics` 버튼 클릭 시 DataGrid로 표시:

| 컬럼 | 내용 |
|------|------|
| Node | 노드 이름 |
| Exec | 총 실행 횟수 |
| Success% | 성공률 (소수점 1자리) |
| Avg(ms) | 평균 소요 시간 |
| Min(ms) | 최소 소요 시간 |
| Max(ms) | 최대 소요 시간 |

**Refresh**: 최신 통계로 DataGrid 갱신  
**Reset**: 전체 통계 초기화 (확인 대화상자 포함)

---

## 전역 설정

```csharp
// 통계 트래커를 전역 정적으로 설정 (모든 ActionNodeBase에서 접근)
ActionNodeBase.SetStatsTracker(statsTracker);
```

---

## 함수 설명

### `NodeExecutionStats`

#### `RecordExecution(long executionTimeMs, bool success, string? error)`
- 단일 노드 통계 객체에 실행 결과를 반영한다.
- 실행 횟수, 성공/실패 수, 누적/최소/최대 실행 시간, 마지막 오류와 마지막 실행 시각을 갱신한다.

### `NodeStatsTracker`

#### `RecordExecution(string nodeName, long executionTimeMs, bool success, string? error = null)`
- 노드 이름별 통계 객체를 가져오거나 새로 만들고 실행 결과를 기록한다.

#### `NodeExecutionStats? GetStats(string nodeName)`
- 특정 노드의 통계를 조회한다.

#### `List<NodeExecutionStats> GetAllStats()`
- 모든 노드 통계를 실행 횟수 내림차순으로 반환한다.

#### `void Reset()`
- 전체 통계를 초기화한다.

#### `void ResetNode(string nodeName)`
- 특정 노드 통계만 제거한다.

---

## 동시성 및 측정 범위

항목 사전은 `ConcurrentDictionary`, 단일 항목 갱신은 내부 `Lock`으로 보호한다. 조회는 같은 객체의 참조를 반환하며 여러 속성을 한 번에 고정한 스냅샷은 아니다. 병렬 기록 중 조회값들이 모두 같은 순간의 상태라고 가정하지 않는다.

시간에는 측정 구간 안에서 동기 실행한 후속 노드가 포함된다. 분기를 직접 실행하는 노드도 호출이 반환하기 전에 수행한 자식 실행 시간은 포함하지만, `Task.Run` 등으로 시작한 비동기 작업 전체 완료 시간은 보장하지 않는다.

성공은 해당 측정 호출에서 예외가 밖으로 전파되지 않았다는 뜻이다. 동기 후속 노드의 예외가 부모까지 전파되면 부모도 실패로 기록된다. 노드 내부에서 삼킨 예외나 반환 후 비동기 실패는 반영되지 않을 수 있으며, 이 값은 원격 패킷 전달 성공률과 다르다.

## 관련 문서
- [[BotActionGraphWindow]] — 통계 창 표시 버튼
- [[BotActionGraph]] — ExecuteChainWithStats 호출
