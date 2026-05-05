# 빠른 시작 가이드 (Getting Started)

> 콘텐츠 서버/클라이언트를 구현할 때 최소한으로 필요한 절차.

---

## 서버 측

### 1. 패킷 정의

`Tool/PacketDefine.yml`에 패킷 추가:

```yaml
Packet:
  - Type: RequestPacket
    PacketName: MyRequest
    Items:
      - Type: int
        Name: value
  - Type: ReplyPacket
    PacketName: MyResponse
    Items:
      - Type: int
        Name: result
```

`Tool/PacketGenerate.bat` 실행 → 코드 자동 생성

---

### 2. Player 클래스 구현

`Player.h` (자동 생성됨):
```cpp
class Player final : public RUDPSession {
public:
    explicit Player(MultiSocketRUDPCore& inCore);
private:
    void OnConnected() override;
    void OnDisconnected() override;
    void RegisterAllPacketHandler();

#pragma region Packet Handler
public:
    void OnMyRequest(const MyRequest& packet);
#pragma endregion Packet Handler
};
```

`Player.cpp`:
```cpp
Player::Player(MultiSocketRUDPCore& inCore) : RUDPSession(inCore) {
    RegisterAllPacketHandler();
}
void Player::OnConnected() { /* 초기화 */ }
void Player::OnDisconnected() { /* 정리 */ }
void Player::RegisterAllPacketHandler() {
    RegisterPacketHandler<Player, MyRequest>(
        static_cast<PacketId>(PACKET_ID::MY_REQUEST),
        &Player::OnMyRequest
    );
}
void Player::OnMyRequest(const MyRequest& packet) {
    MyResponse res;
    res.result = packet.value * 2;
    SendPacket(res);
}
```

---

### 3. 서버 실행

```cpp
MultiSocketRUDPCore core(L"MY", L"DevServerCert");
ContentsPacketRegister::Init();  // 패킷 팩토리 등록 (StartServer 이전에 호출)
core.StartServer(
    L"ServerOptionFile/CoreOption.txt",
    L"ServerOptionFile/SessionBrokerOption.txt",
    [](MultiSocketRUDPCore& c) { return new Player(c); },
    true  // 콘솔 출력
);

// ... 메인 루프
core.StopServer();
```

---

## 클라이언트 측

### 1. 패킷 핸들러 등록

```cpp
PacketManager::RegisterPacket<MyResponse>();
PacketManager::RegisterPacketHandler<MyResponse>(myResponseHandler);
```

### 2. 연결 및 송수신

```cpp
RUDPClientCore client;
client.Start(L"ClientOption.ini", L"SessionGetterOption.ini", true);

// 패킷 전송
MyRequest req;
req.value = 42;
client.SendPacket(req);

// 수신 루프
while (client.IsConnected()) {
    NetBuffer* buf = client.GetReceivedPacket();
    if (buf) {
        PacketId id;
        *buf >> id;
        // 처리...
        NetBuffer::Free(buf);
    }
}

client.Disconnect();
```

---

## TLS 인증서 설정 (개발 환경)

```batch
:: 자체 서명 인증서 생성
Tool\ForTLS\CreateDevTLSCert.bat

:: 제거
Tool\ForTLS\RemoveDevTLSCert.bat
```

생성된 인증서: `CN=DevServerCert` (Windows 인증서 저장소 `MY`)

---

## 옵션 파일 주요 항목

### CoreOption.ini (서버)

```ini
[CORE]
THREAD_COUNT=4
NUM_OF_SOCKET=100
MAX_PACKET_RETRANSMISSION_COUNT=10
WORKER_THREAD_ONE_FRAME_MS=1
RETRANSMISSION_MS=200
RETRANSMISSION_THREAD_SLEEP_MS=100
HEARTBEAT_THREAD_SLEEP_MS=3000
TIMER_TICK_MS=16
MAX_HOLDING_PACKET_QUEUE_SIZE=32

[SERIALIZEBUF]
PACKET_CODE=0x89
PACKET_KEY=0x99
```

### SessionBrokerOption.ini

```ini
[SESSION_BROKER]
CORE_IP=127.0.0.1
SESSION_BROKER_PORT=10000
```

### ClientOption.ini

```ini
[CORE]
MAX_PACKET_RETRANSMISSION_COUNT=10
RETRANSMISSION_MS=200
SERVER_ALIVE_CHECK_MS=5000
```

---

## 관련 문서
- [[PacketGenerator]] — 패킷 코드 자동 생성 상세
- [[RUDPSession]] — 세션 구현 가이드
- [[RUDPClientCore]] — 클라이언트 API
- [[Glossary]] — 용어 참고

---

## BotTester — 봇 테스트 실행

### 1. 행동 트리 설계

`Set Bot Action Graph` 버튼 → `BotActionGraphWindow` 오픈

**방법 A: 직접 설계**
1. 좌측 노드 목록에서 타입 선택 → `Add Node`
2. 노드 더블클릭 → PacketId, Delay 등 설정
3. 출력 포트 드래그 → 다른 노드 입력 포트에 연결

**방법 B: AI 자동 생성**
1. `AI Tree Generator` 버튼 클릭
2. 자연어로 시나리오 입력 (예: "Ping을 3번 보내고 Pong을 기다려라")
3. `Generate Tree` → 검증 통과 시 `Apply to Canvas`

### 2. 빌드 및 적용

```
Validate Graph   → 오류/경고 확인
Build Graph      → ActionGraph 객체 생성
Apply to BotTester → BotTesterCore에 적용 + 비주얼 저장
```

### 3. 봇 실행

MainWindow에서:
- **Host IP / Port**: SessionBroker 서버 주소
- **Insert Bot Count**: 생성할 봇 수
- `Start Bot Test` → N개 봇 생성 및 연결

### 4. BotVariables 확장 (선택)

패킷 파서 + 조건 평가에 사용할 변수를 `BotVariables.cs`에 등록:

```csharp
public static class BotVariables
{
    [BotVariable("myScore", "내 점수", VariableAccessType.GetAndSet)]
    public static int GetMyScore(RuntimeContext ctx)
        => ctx.GetOrDefault("myScore", 0);

    [BotVariable("myScore", "내 점수 설정", VariableAccessType.Set)]
    public static void SetMyScore(RuntimeContext ctx, NetBuffer? buffer)
    {
        if (buffer != null) ctx.Set("myScore", (int)buffer.ReadUInt());
    }
}
```

### 5. Gemini API 키 설정

`WithGeminiClient/GeminiClientConfiguration.json`:
```json
{ "GeminiSettings": { "ApiKey": "YOUR_GEMINI_API_KEY", ... } }
```

---

## 관련 문서
- [[BotTester/00_BotTester_Overview|BotTester 개요]]
- [[ActionNodes]] — 사용 가능한 노드 타입
- [[AiTreeGenerator]] — AI 생성 상세
---

## 검토 메모

- 서버 TLS 생성자 예시는 현재 코드 기준으로 `MultiSocketRUDPCore core(L"MY", L"DevServerCert");` 형태가 맞다.
- 브로커 인증서 설정 흐름은 `TLSHelperServer` 생성자 인자로 내려가므로, 예전 `Initialize(storeName, subjectName)` 설명을 그대로 재사용하면 현재 구현과 어긋난다.
