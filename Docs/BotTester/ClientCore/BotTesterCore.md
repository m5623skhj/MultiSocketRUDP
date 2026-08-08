# BotTesterCore (봇 라이프사이클 관리)

> 봇 인스턴스를 생성·관리하는 싱글톤.  
> 연결 정보 설정 → SessionBroker에서 세션 수신 → `Client` N개 생성 → ActionGraph 적용.

---

## 주요 흐름

```
MainWindow
  ├─ SetConnectionInfo(ip, port)
  ├─ SetBotActionGraph(graph)        ← BotActionGraphWindow에서 Apply 시
  └─ StartBotTest(numOfBot)
       └─ for i in 0..numOfBot:
            GetSessionInfoFromSessionBroker()
              └─ SessionGetter.ConnectAsync(ip, port)  ← TLS
              └─ ReceiveAsync()  ← 세션 정보 수신
              └─ new Client(sessionInfoBytes)
                   └─ ParseSessionBrokerResponse()
                        ├─ serverIp, serverPort, sessionId, sessionKey, sessionSalt 파싱
                        └─ AesGcm 초기화 + UDP 연결 + ReceiveAsync + RetransmissionAsync
            client.SetActionGraph(botActionGraph)
            sessionDictionary.Add(sessionId, client)
```

### `SetConnectionInfo`

```csharp
public void SetConnectionInfo(string targetIp, ushort targetPort)
```

연결 대상 서버의 IP와 포트를 설정한다.

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `targetIp` | `string` | 서버 IP 주소 |
| `targetPort` | `ushort` | 서버 포트 번호 |

**예외:** `targetIp`가 비어있거나 `targetPort`가 0일 경우 `ArgumentException`이 발생한다.
## 세션 브로커 패킷 수신

```
패킷 헤더: 5 bytes (GlobalConstants.PacketHeaderSize)
  [0]:    HeaderCode (1B)
  [1-2]:  PayloadLength (2B, GlobalConstants.PayloadPosition)
  [3-4]:  (reserved)

페이로드:
  [0]:    ConnectResultCode (1B)
  [1..]:  serverIp (string: len(2B) + bytes)
          serverPort (2B)
          sessionId (2B)
          sessionKey (16B)
          sessionSalt (16B)
```

---

## API

```csharp
void SetConnectionInfo(string ip, ushort port)
void SetBotActionGraph(ActionGraph graph)
void SaveGraphVisuals(List<NodeVisual>)    // 에디터 상태 저장
void ClearSavedGraphVisuals()
List<NodeVisual>? GetSavedGraphVisuals()  // 에디터 재오픈 시 복원

async Task StartBotTest(ushort numOfBot)
Task<RttTestSummary> StartRttTest(int sampleCount, int timeoutMs)
Task<RttTestSummary> StartRttTest(int sampleCount, int timeoutMs, double lossRate, int lossSeed)
void StopBotTest()                        // 전체 Client.Disconnect()
int GetActiveBotCount()                   // IsConnected() == true 개수
```

---

## 함수 설명

#### `SetConnectionInfo(string targetIp, ushort targetPort)`
- SessionBroker에 접속할 대상 IP와 포트를 저장한다.

#### `SetBotActionGraph(ActionGraph graph)`
- 이후 생성되는 봇들에게 적용할 행동 그래프를 저장한다.

#### `SaveGraphVisuals(List<NodeVisual> nodeVisuals)`
- 에디터 재오픈 시 복원할 수 있도록 현재 노드 비주얼 상태를 저장한다.

#### `ClearSavedGraphVisuals()`
- 저장해 둔 비주얼 상태를 제거한다.

#### `List<NodeVisual>? GetSavedGraphVisuals()`
- 마지막으로 저장한 비주얼 상태를 반환한다.

#### `StartBotTest`

```csharp
public async Task StartBotTest(ushort numOfBot)
```

지정한 수만큼 `SessionBroker`에서 세션 정보를 받아 봇 테스트를 시작하고, 각 세션에 행동 그래프와 RTT 추적기를 적용한다.

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `numOfBot` | `ushort` | 테스트할 봇의 수 |

**예외**
- `InvalidOperationException`: 연결 정보(`hostIp`, `hostPort`)가 설정되지 않은 상태에서 호출된 경우

**주의사항**
- 호출 전 `SetConnectionInfo`를 통해 연결 정보가 설정되어 있어야 한다.
- 내부적으로 `activeBotTestCompletion`을 사용하여 기존에 진행 중인 봇 테스트가 있다면 취소 처리한다.
#### `Task<RttTestSummary> StartRttTest(...)`
- SessionBroker에서 단일 세션을 받아 RTT 모드로 전환한 뒤, 지정한 표본 수와 timeout으로 RTT 테스트를 실행한다.
- 두 번째 overload는 양방향 패킷 손실률과 난수 seed를 함께 지정한다.

#### `void StopBotTest()`

```csharp
public void StopBotTest()
```

현재 활성화된 봇 테스트를 중단한다. 관련 컴플리션 트래커(completion tracker)를 취소하고 모든 활성 봇 세션의 연결을 강제로 해제한다. 세션은 내부 딕셔너리에서 즉시 제거된다.
#### `int GetActiveBotCount()`
- 현재 연결된 봇 수를 반환한다.

#### `Task<Client?> GetSessionInfoFromSessionBroker()`
- SessionBroker에 TLS로 접속해 세션 정보를 받아 새 `Client`를 생성한다.

---

## 관련 문서
- [[RudpSession_CS]] — Client가 상속하는 세션 클래스
- [[SessionGetter_CS]] — TLS 세션 정보 수신
- [[BotActionGraph]] — 봇에 적용되는 행동 트리
- [[PacketLossSimulator]] — RTT 테스트의 양방향 손실 시뮬레이션
- [[BufferStore]] — 세션의 미응답 송신 패킷 추적
