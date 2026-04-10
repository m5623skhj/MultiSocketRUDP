# RuntimeContext

> 노드 체인 실행 중 데이터를 공유하는 스레드 안전 컨텍스트 객체.  
> 각 `Client` 인스턴스는 `GlobalContext`를 하나씩 보유한다.

---

## 구조

```csharp
public class RuntimeContext(Client client, NetBuffer? packet)
{
    public Client Client { get; }                  // 소유 클라이언트
    private NetBuffer? currentPacket;              // 현재 처리 중인 패킷 (Lock 보호)
    private ConcurrentDictionary<string, object> vars;  // 변수 저장소
}
```

---

## 핵심 API

### 패킷 접근

```csharp
NetBuffer? GetPacket()        // 현재 패킷 반환 (packetLock 보호)
void SetPacket(NetBuffer?)    // 패킷 설정 (ContextNodeBase.Execute에서 자동 호출)
```

### 변수 저장/조회

```csharp
void Set<T>(string key, T value)             // 저장
T Get<T>(string key)                         // 조회 (없으면 KeyNotFoundException)
T GetOrDefault<T>(string key, T defaultValue) // 조회 (없으면 기본값)
bool Has(string key)                          // 존재 여부
int AtomicIncrement(string key, int delta=1)  // 원자적 증가
```

---

## 예약 키 컨벤션

| 키 패턴 | 설정 주체 | 내용 |
|---------|-----------|------|
| `__received_{PacketId}` | `Client.OnRecvPacket` | 수신된 NetBuffer |
| `__received_{PacketId}_timestamp` | `Client.OnRecvPacket` | 수신 시각 (ulong ms) |
| `__loop_iteration` | `LoopNode` | 현재 반복 인덱스 |
| `__repeat_iteration_{guid}` | `RepeatTimerNode` | 현재 반복 인덱스 |
| `__exec_{nodeName}` | `NodeExecutionHelper` | 노드 실행 횟수 |
| `__metric_{name}` | `RecordMetric()` | 성능 지표 샘플 목록 |
| `__timer_{name}` | `StartTimer()` | 타이머 시작 시각 |

---

## `RuntimeContext`

```csharp
public class RuntimeContext(Client client, NetBuffer? packet)
```

클라이언트의 네트워크 컨텍스트를 관리하며, 패킷 데이터와 세션 변수를 유지한다.

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `client` | `Client` | 이 컨텍스트를 소유하는 클라이언트 인스턴스 |
| `packet` | `NetBuffer?` | 트리거된 수신 패킷. 없으면 `null` |

### `GetPacket`

```csharp
public NetBuffer? GetPacket()
```

현재 설정된 패킷을 반환한다. 스레드 안전하게 동작한다.

### `SetPacket`

```csharp
public void SetPacket(NetBuffer? newPacket)
```

현재 패킷을 새로 설정한다. 스레드 안전하게 동작한다.

### `Set<T>`

```csharp
public void Set<T>(string key, T value) where T : notnull
```

컨텍스트 내에 키-값 쌍을 저장한다.

---

## 확장 메서드 (`RuntimeContextExtensions`)

```csharp
// 플래그
ctx.SetFlag("hasItem");
ctx.ClearFlag("hasItem");
bool isSet = ctx.IsFlagSet("hasItem");

// 카운터
ctx.ResetCounter("hits");
int n = ctx.GetCounter("hits");
int next = ctx.Increment("hits");

// 타이머
ctx.StartTimer("phase1");
TimeSpan? elapsed = ctx.GetElapsed("phase1");

// 성능 지표
ctx.RecordMetric("rtt_ms", 42.5);
double avg = ctx.GetAverageMetric("rtt_ms");
double min = ctx.GetMinMetric("rtt_ms");
double max = ctx.GetMaxMetric("rtt_ms");

// 노드 실행 횟수
int count = ctx.GetExecutionCount("SendPing");
```
## WaitForPacketNode와의 연동

`Client.OnRecvPacket`에서 패킷 수신 시 자동으로 컨텍스트에 버퍼를 저장:

```csharp
GlobalContext.Set($"__received_{packetId}", buffer);
GlobalContext.Set($"__received_{packetId}_timestamp", CommonFunc.GetNowMs());
```

`WaitForPacketNode`는 이 키를 폴링(50ms 간격)해서 수신 여부를 확인한다.

---

## 관련 문서
- [[ActionNodes]] — 컨텍스트를 사용하는 노드들
- [[BotActionGraph]] — 컨텍스트 생성 및 전달
