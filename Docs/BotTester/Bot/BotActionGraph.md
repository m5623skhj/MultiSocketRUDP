# BotActionGraph (행동 트리 실행 엔진)

> 트리거 기반 행동 그래프 실행 엔진.  
> 노드를 트리거 타입별로 분류해 보관하고, 이벤트 발생 시 해당 노드 체인을 실행한다.

---

## 구조

```
ActionGraph
 ├── triggerNodes: Dictionary<TriggerType, List<ActionNodeBase>>
 ├── packetTriggerNodes: Dictionary<PacketId, List<ActionNodeBase>>
 └── allNodes: List<ActionNodeBase>
```

---

## 트리거 타입 (`TriggerType`)

| 값 | 발생 시점 |
|----|-----------|
| `OnConnected` | 서버 연결 완료 |
| `OnDisconnected` | 연결 해제 |
| `OnPacketReceived` | 특정 패킷 수신 |
| `Manual` | 코드에서 직접 호출 |

---

## 이벤트 실행 흐름

```
ActionGraph.TriggerEvent(client, triggerType, packetId?, buffer?)
  ├─ ResolveCandidates(triggerType, packetId)
  │    ├─ OnPacketReceived → packetTriggerNodes[packetId]
  │    └─ 기타 → triggerNodes[triggerType]
  │
  └─ foreach candidate where trigger.Matches(...):
       NodeExecutionHelper.ExecuteChainWithStats(client, node, buffer, visited)
```

### TriggerCondition.Matches 검증 순서
1. `Type` 일치 여부
2. (OnPacketReceived) `PacketId` 일치 여부
3. `PacketValidator(buffer)` 호출 (설정된 경우)

---

## ActionGraphBuilder (플루언트 API)

```csharp
var graph = new ActionGraphBuilder()
    .WithName("PingPong Test")
    .OnConnected("Start")
    .ThenSend("SendPing", PacketId.Ping, _ => new NetBuffer())
    .OnReceive("WaitPong", PacketId.Pong)
    .ThenLog("LogPong", (client, _) => $"Pong: {client.GetSessionId()}")
    .Build();
```

| 메서드 | 생성 노드 |
|--------|-----------|
| `OnConnected(name)` | CustomActionNode (OnConnected 트리거) |
| `OnReceive(name, packetId)` | CustomActionNode (OnPacketReceived) |
| `ThenSend(name, id, builder)` | SendPacketNode |
| `ThenDo(name, action)` | CustomActionNode |
| `ThenLog(name, msgBuilder)` | LogNode |
| `ThenWait(name, ms)` | DelayNode |
| `ThenIf(name, condition)` | ConditionalNode + ConditionalBranchBuilder |
| `ThenLoop(name, condition)` | LoopNode + LoopBranchBuilder |
| `ThenRepeat(name, count, ms)` | RepeatTimerNode + RepeatBranchBuilder |

---

## 동시성

| 자원 | 보호 수단 |
|------|-----------|
| `allNodes` | `Lock allNodesLock` |
| `triggerNodes` | `Lock triggerNodesLock` |
| `packetTriggerNodes` | `Lock packetTriggerNodesLock` |

---

## 함수 설명

### `ActionGraph`

#### `List<ActionNodeBase> GetAllNodes()`
- 현재 그래프에 등록된 모든 노드를 복사본 리스트로 반환한다.
- 내부 컬렉션을 그대로 노출하지 않기 때문에 검증기나 UI가 안전하게 순회할 수 있다.

#### `void AddNode(ActionNodeBase node)`
- 그래프 노드 하나를 등록하고, 트리거가 있으면 `triggerNodes`와 `packetTriggerNodes` 인덱스에도 함께 반영한다.
- `OnPacketReceived` 트리거는 `PacketId` 기준 별도 인덱스를 유지한다.

#### `void TriggerEvent(Client client, TriggerType triggerType, PacketId? packetId = null, NetBuffer? buffer = null)`
- 특정 이벤트에 맞는 후보 노드를 찾아 실행 체인을 시작한다.
- 실제 노드 실행은 `NodeExecutionHelper.ExecuteChainWithStats()`를 통해 통계와 함께 수행된다.

#### `List<ActionNodeBase>? ResolveCandidates(TriggerType triggerType, PacketId? packetId)`
- 트리거 종류와 패킷 ID에 맞는 후보 노드 목록을 선택한다.
- 패킷 트리거는 `packetTriggerNodes`, 일반 트리거는 `triggerNodes`에서 검색한다.

### `ActionGraphBuilder`

#### `ActionGraph Build()`
- 현재까지 누적한 노드 체인을 포함하는 `ActionGraph`를 반환한다.

#### `ActionGraphBuilder WithName(string name)`
- 결과 그래프 이름을 설정한다.

#### `ActionGraphBuilder OnConnected(string nodeName)`
#### `ActionGraphBuilder OnDisconnected(string nodeName)`
#### `ActionGraphBuilder OnReceive(string nodeName, PacketId packetId, Func<NetBuffer, bool>? validator = null)`
- 트리거가 있는 시작 노드를 생성해 그래프 루트 체인을 시작한다.

#### `ActionGraphBuilder ThenSend(...)`
#### `ActionGraphBuilder ThenDo(...)`
#### `ActionGraphBuilder ThenLog(...)`
#### `ActionGraphBuilder ThenWait(...)`
- 마지막 노드 뒤에 일반 동작 노드를 이어 붙인다.

#### `ConditionalBranchBuilder ThenIf(...)`
#### `LoopBranchBuilder ThenLoop(...)`
#### `RepeatBranchBuilder ThenRepeat(...)`
- 분기/루프/반복을 포함하는 복합 빌더로 진입한다.

#### `ConditionalBranchBuilder.OnTrue(...)`, `OnFalse(...)`, `EndIf()`
- 조건 분기의 true/false 체인을 구성하고 부모 빌더로 복귀한다.

#### `LoopBranchBuilder.Do(...)`, `OnExit(...)`, `EndLoop()`
- 루프 본문과 종료 체인을 구성한다.

#### `RepeatBranchBuilder.Do(...)`, `EndRepeat()`
- 반복 본문 체인을 구성한다.

---

## 관련 문서
- [[ActionNodes]] — 모든 노드 타입
- [[RuntimeContext]] — 실행 컨텍스트
- [[BotActionGraphWindow]] — 비주얼 에디터에서 그래프 빌드
- [[BotTesterCore]] — 그래프를 Client에 적용
