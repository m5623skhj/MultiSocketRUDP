# 빠른 시작 가이드 (Getting Started)

> 현재 저장소 기준으로 콘텐츠 서버, C++ 클라이언트, BotTester를 가장 짧게 올리는 절차만 정리한다.

---

## 서버 측

### 1. 패킷 정의

`Tool/PacketDefine.yml`에 패킷을 추가한 뒤 `Tool/PacketGenerate.bat`를 실행한다.

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

생성 결과물은 `Protocol.*`, `PacketIdType.h`, `Player.*`, `ContentsPacketRegister.*`에 반영된다.

### 2. 세션 클래스 구현

`Player`는 `RUDPSession`을 상속하고 생성자에서 패킷 핸들러를 등록한다.

```cpp
class Player final : public RUDPSession
{
public:
    explicit Player(MultiSocketRUDPCore& inCore);

private:
    void OnConnected() override;
    void OnDisconnected() override;
    void OnReleased() override;

    void OnMyRequest(const MyRequest& packet);
};
```

```cpp
Player::Player(MultiSocketRUDPCore& inCore)
    : RUDPSession(inCore)
{
    RegisterPacketHandler<Player, MyRequest>(
        static_cast<PacketId>(PACKET_ID::MY_REQUEST),
        &Player::OnMyRequest);
}

void Player::OnMyRequest(const MyRequest& packet)
{
    MyResponse res;
    res.result = packet.value * 2;
    SendPacket(res);
}
```

### 3. 서버 시작

현재 저장소의 실제 시작 순서는 `ContentsPacketRegister::Init()` 호출 후 `StartServer(...)`다.

```cpp
MultiSocketRUDPCore core(L"MY", L"DevServerCert");

ContentsPacketRegister::Init();

if (!core.StartServer(
        L"ServerOptionFile/CoreOption.txt",
        L"ServerOptionFile/SessionBrokerOption.txt",
        [](MultiSocketRUDPCore& inCore) -> RUDPSession*
        {
            return new Player(inCore);
        },
        true))
{
    core.StopServer();
    return -1;
}

// ...
core.StopServer();
```

---

## C++ 클라이언트 측

### 1. 중요한 전제

`RUDPClientCore::Start()`와 `Stop()`은 `protected`다.  
즉 `RUDPClientCore client; client.Start(...);` 형태는 현재 코드에서 사용할 수 없다.

현재 저장소의 예제는 `ContentsClient/main.cpp`처럼 `RUDPClientCore`를 상속한 테스트 클라이언트를 사용한다.

### 2. 실제 시작 경로

```cpp
class TestClient final : public RUDPClientCore
{
public:
    using RUDPClientCore::Start;
    using RUDPClientCore::Stop;
};
```

```cpp
TestClient client;

if (!client.Start(
        L"ClientOptionFile/CoreOption.txt",
        L"ClientOptionFile/SessionGetterOption.txt",
        true))
{
    client.Stop();
    return -1;
}
```

### 3. 송수신 예시

```cpp
MyRequest req;
req.value = 42;
client.SendPacket(req);

while (client.IsConnected())
{
    NetBuffer* buf = client.GetReceivedPacket();
    if (buf == nullptr)
    {
        Sleep(1);
        continue;
    }

    PacketId packetId;
    *buf >> packetId;
    // packetId 기준 처리

    NetBuffer::Free(buf);
}

client.Disconnect();
```

---

## TLS 인증서 설정

개발 환경에서는 아래 배치 파일을 사용한다.

```batch
Tool\ForTLS\CreateDevTLSCert.bat
Tool\ForTLS\RemoveDevTLSCert.bat
```

기본 예시는 `CN=DevServerCert`, 저장소 이름 `MY`를 사용한다.

---

## 옵션 파일 경로

현재 저장소의 기본 경로는 아래와 같다.

### 서버

```text
ServerOptionFile/CoreOption.txt
ServerOptionFile/SessionBrokerOption.txt
```

### C++ 클라이언트

```text
ClientOptionFile/CoreOption.txt
ClientOptionFile/SessionGetterOption.txt
```

예전 문서에 있던 `ClientOption.ini`, `SessionGetterOption.ini` 경로는 현재 샘플 코드 기준이 아니다.

---

## BotTester

### 실행

BotTester는 SessionBroker에서 세션 정보를 받은 뒤 UDP 세션을 연다.  
Host/Port에는 SessionBroker 주소를 넣는다.

### 변수 확장

`BotVariables`는 `MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/Bot/BotVariableAttribute.cs`에 정의되어 있다.
Getter/Setter 메서드는 이 클래스에 추가하고 `BotVariableAttribute`로 노출한다. `VariableNodes.cs`는 등록된 접근자를 실행하는 노드 구현이다.

### Gemini 설정

Gemini 설정 파일 위치는 아래다.

```text
MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/WithGeminiClient/GeminiClientConfiguration.json
```

---

## 주의사항

- `ContentsPacketRegister::Init()`은 반드시 `StartServer()` 이전에 호출한다.
- `RUDPSession::DoDisconnect()`는 현재 `DISCONNECT_REASON` 인자를 받는다.
- `MultiSocketRUDPCore::GetUsingSession()`은 콘텐츠 코드에서 직접 호출하는 공개 API가 아니다.
- SessionBroker 응답의 `CONNECT_RESULT_CODE`는 현재 `1B` enum이다.

---

## 관련 문서

- [[ContentServerGuide]] - 콘텐츠 서버 구성 상세
- [[RUDPSession]] - 세션 상속 포인트
- [[RUDPClientCore]] - C++ 클라이언트 내부 동작
- [[RudpSession_CS]] - BotTester C# 세션 구현
