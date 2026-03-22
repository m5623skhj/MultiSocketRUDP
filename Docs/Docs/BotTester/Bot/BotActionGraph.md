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

## 관련 문서
- [[ActionNodes]] — 모든 노드 타입
- [[RuntimeContext]] — 실행 컨텍스트
- [[BotActionGraphWindow]] — 비주얼 에디터에서 그래프 빌드
- [[BotTesterCore]] — 그래프를 Client에 적용
