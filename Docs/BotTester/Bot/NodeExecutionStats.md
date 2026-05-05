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

`NodeExecutionHelper.ExecuteChainWithStats`에서 각 노드 실행 전후에 자동 기록:

```
Stopwatch.Start()
node.Execute(client, buffer)
Stopwatch.Stop()
→ statsTracker.RecordExecution(nodeName, elapsed, success, error?)
→ ctx.IncrementExecutionCount(nodeName)
→ ctx.RecordMetric("{nodeName}_time", elapsed)
```

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

## 관련 문서
- [[BotActionGraphWindow]] — 통계 창 표시 버튼
- [[BotActionGraph]] — ExecuteChainWithStats 호출
