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

---

## 세션 브로커 패킷 수신

```
패킷 헤더: 5 bytes (GlobalConstants.PacketHeaderSize)
  [0]:    HeaderCode (1B)
  [1-2]:  PayloadLength (2B, GlobalConstants.PayloadPosition)
  [3-4]:  (reserved)

페이로드:
  [0..3]: RUDP_PROTOCOL_VERSION (4B, little-endian, 현재 2)
  [4]:    ConnectResultCode (1B)
  [5..]:  serverIp (string: len(2B) + bytes)
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

#### `Task StartBotTest(ushort numOfBot)`
- 지정한 수만큼 SessionBroker에서 세션 정보를 받아 `Client` 인스턴스를 만들고 행동 그래프를 적용한다.

#### `Task<RttTestSummary> StartRttTest(...)`
- SessionBroker에서 단일 세션을 받아 RTT 모드로 전환한 뒤, 지정한 표본 수와 timeout으로 RTT 테스트를 실행한다.
- 두 번째 overload는 양방향 패킷 손실률과 난수 seed를 함께 지정한다.

#### `void StopBotTest()`
- 현재 관리 중인 모든 클라이언트 봇을 정리하고 연결을 끊는다.

#### `int GetActiveBotCount()`
- 현재 연결된 봇 수를 반환한다.

#### `Task<Client?> GetSessionInfoFromSessionBroker()`
- SessionBroker에 TLS로 접속해 세션 정보를 받아 새 `Client`를 생성한다.

---

## 완료 통지와 부하 측정

`StartBotTest()`는 봇별 RTT 수집기와 완료 추적기를 설치한다. 모든 봇의 설정이 끝나고 전체 연결이 해제되면 `BotTestCompleted` 이벤트를 한 번 통지한다. `StopBotTest()`는 추적기를 취소하고 세션을 정리하므로 정상 완료 통지를 만들지 않는다.

`StartRttStressTest(RttStressTestConfiguration, CancellationToken)`는 모든 클라이언트를 먼저 연결한 뒤 각 클라이언트에 요청 하나만 진행 중인 폐루프 부하를 실행한다. 워밍업·측정 시간·재전송 옵션을 검증하고 종료 시 생성한 클라이언트를 정리한다. 최대 처리량을 보장하는 측정은 아니다.

세션 사전은 `sessionDictionaryLock`으로 보호하지만 연결용 `SessionGetter`는 공유 인스턴스다. 설정 변경과 테스트 시작·중지는 UI 또는 제어 계층에서 직렬화해야 한다.

## 로그와 인증의 현재 제한

`GetSessionInfoFromSessionBroker()`는 Debug 로그에 브로커 응답 전체의 16진 바이트를 기록한다. 성공 응답에는 세션 키·솔트가 포함되므로 해당 로그를 외부 공유하거나 운영 수집에 그대로 사용하면 안 된다. 기본 `SessionGetter` 호출은 지문을 전달하지 않아 인증서 검증을 생략한다. 두 동작은 현재 구현의 제한이며 이 문서 갱신에서는 코드를 바꾸지 않는다.

## 관련 문서
- [[RudpSession_CS]] — Client가 상속하는 세션 클래스
- [[SessionGetter_CS]] — TLS 세션 정보 수신
- [[BotActionGraph]] — 봇에 적용되는 행동 트리
- [[PacketLossSimulator]] — RTT 테스트의 양방향 손실 시뮬레이션
- [[BufferStore]] — 세션의 미응답 송신 패킷 추적
