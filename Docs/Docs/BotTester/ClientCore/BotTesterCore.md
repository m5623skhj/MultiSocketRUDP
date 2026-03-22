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
List<NodeVisual>? GetSavedGraphVisuals()  // 에디터 재오픈 시 복원

async Task StartBotTest(ushort numOfBot)
void StopBotTest()                        // 전체 Client.Disconnect()
int GetActiveBotCount()                   // IsConnected() == true 개수
```

---

## 관련 문서
- [[RudpSession_CS]] — Client가 상속하는 세션 클래스
- [[SessionGetter_CS]] — TLS 세션 정보 수신
- [[BotActionGraph]] — 봇에 적용되는 행동 트리
