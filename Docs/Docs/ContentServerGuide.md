# 콘텐츠 서버 개발 가이드

> **처음부터 콘텐츠 서버를 구축하는 단계별 가이드.**  
> 이 문서를 따라가면 MultiSocketRUDP 위에서 동작하는 에코 서버 → 채팅 서버 → 게임 서버로 확장할 수 있다.

---

## 목차

1. [Step 0: 환경 준비](#step-0-환경-준비)
2. [Step 1: 최소 에코 서버](#step-1-최소-에코-서버)
3. [Step 2: 패킷 정의와 자동 생성](#step-2-패킷-정의와-자동-생성)
4. [Step 3: 세션 상태 관리](#step-3-세션-상태-관리)
5. [Step 4: 브로드캐스트와 룸 시스템](#step-4-브로드캐스트와-룸-시스템)
6. [Step 5: 타이머와 게임 루프](#step-5-타이머와-게임-루프)
7. [Step 6: 전역 변수 접근 패턴](#step-6-전역-변수-접근-패턴)
8. [클라이언트 연동 체크리스트](#클라이언트-연동-체크리스트)

---

## Step 0: 환경 준비

### 필수 파일 구조

```
ContentsServer/
├── PreCompile.h            ← 공용 헤더 (미리 컴파일)
├── PreCompile.cpp
├── main.cpp
├── Player.h / Player.cpp   ← RUDPSession 상속 (PacketGenerator 자동 생성)
├── Protocol.h / .cpp       ← 패킷 클래스 (PacketGenerator 자동 생성)
├── PacketIdType.h          ← PACKET_ID enum (PacketGenerator 자동 생성)
├── PlayerPacketHandlerRegister.h / .cpp  ← Init() (PacketGenerator 자동 생성)
├── ServerOption/
│   ├── CoreOption.ini
│   └── SessionBrokerOption.ini
└── WithGeminiClient/       ← AI (선택, BotTester용)
    └── GeminiClientConfiguration.json
```

### PreCompile.h 최소 설정

```cpp
#pragma once
#include <windows.h>
#include <MSWSock.h>
#include <winsock2.h>

// MultiSocketRUDP 공용 헤더
#include "../MultiSocketRUDPServer/LogExtension.h"
#include "../MultiSocketRUDPServer/Logger.h"
#include "../MultiSocketRUDPServer/PacketManager.h"
#include "NetServerSerializeBuffer.h"
#include "CommonType.h"   // PacketSequence 등 typedef
```

### TLS 인증서 생성

```batch
cd Tool
ForTLS\CreateDevTLSCert.bat
```

→ Windows 인증서 저장소(`certmgr.msc` → 개인)에 `CN=DevServerCert` 등록됨.

---

## Step 1: 최소 에코 서버

`Ping` 수신 → `Pong` 응답하는 최소 서버.

### PacketDefine.yml 작성

```yaml
Packet:
  - Type: RequestPacket
    PacketName: Ping
    Desc: 핑

  - Type: ReplyPacket
    PacketName: Pong
    Desc: 퐁 응답
```

```batch
cd Tool
PacketGenerate.bat
```

→ `Protocol.h`, `PacketIdType.h`, `Player.h`, `PlayerPacketHandlerRegister.cpp` 자동 생성.

### Player 구현

```cpp
// Player.h (자동 생성됨, 핸들러 선언 추가)
#pragma once
#include "RUDPSession.h"
#include "Protocol.h"

class Player final : public RUDPSession {
public:
    explicit Player(MultiSocketRUDPCore& core);
    ~Player() override = default;

private:
    void OnConnected()    override;
    void OnDisconnected() override;

#pragma region Packet Handler
public:
    void OnPing(const Ping& packet);
#pragma endregion Packet Handler
};
```

```cpp
// Player.cpp (자동 생성 스텁에 구현 추가)
#include "PreCompile.h"
#include "Player.h"
#include "PacketIdType.h"

Player::Player(MultiSocketRUDPCore& core) : RUDPSession(core) {
    RegisterPacketHandler<Player, Ping>(
        static_cast<PacketId>(PACKET_ID::PING), &Player::OnPing);
}

void Player::OnConnected() {
    LOG_DEBUG(std::format("Player {} connected", GetSessionId()));
}

void Player::OnDisconnected() {
    LOG_DEBUG(std::format("Player {} disconnected", GetSessionId()));
}

void Player::OnPing(const Ping& /*packet*/) {
    Pong pong;
    SendPacket(pong);
}
```

### main.cpp

```cpp
#include "PreCompile.h"
#include "MultiSocketRUDPCore.h"
#include "Player.h"
#include "PlayerPacketHandlerRegister.h"

int main() {
    MultiSocketRUDPCore core(L"MY", L"DevServerCert");

    if (!core.StartServer(
        L"ServerOption/CoreOption.ini",
        L"ServerOption/SessionBrokerOption.ini",
        [](MultiSocketRUDPCore& c) -> RUDPSession* { return new Player(c); },
        true))
    {
        core.StopServer();
        return -1;
    }

    ContentsPacketRegister::Init();

    std::cout << "Echo server running. Press Enter to stop.\n";
    std::cin.get();

    core.StopServer();
    return 0;
}
```

---

## Step 2: 패킷 정의와 자동 생성

### 패킷에 데이터 추가

```yaml
Packet:
  - Type: RequestPacket
    PacketName: ChatReq
    Desc: 채팅 요청
    Items:
      - Type: std::string
        Name: message

  - Type: ReplyPacket
    PacketName: ChatRes
    Desc: 채팅 응답
    Items:
      - Type: SessionIdType
        Name: senderId
      - Type: std::string
        Name: message
```

**자동 생성되는 Protocol.h:**

```cpp
class ChatReq final : public IPacket {
public:
    [[nodiscard]] PacketId GetPacketId() const override;
    void BufferToPacket(NetBuffer& buffer) override;   // buffer >> message
    void PacketToBuffer(NetBuffer& buffer) override;   // buffer << message
public:
    std::string message;
};
```

**직렬화 구현 (자동 생성):**

```cpp
// Protocol.cpp
void ChatReq::BufferToPacket(NetBuffer& buffer) {
    SetBufferToParameters(buffer, message);
    // → buffer >> message  (string: len(2B) + bytes)
}

void ChatReq::PacketToBuffer(NetBuffer& buffer) {
    SetParametersToBuffer(buffer, message);
    // → buffer << message
}
```

### 지원 데이터 타입

| C++ 타입 | 직렬화 크기 |
|----------|------------|
| `BYTE` / `unsigned char` | 1 byte |
| `short` | 2 bytes |
| `int` | 4 bytes |
| `unsigned int` | 4 bytes |
| `long long` | 8 bytes |
| `float` | 4 bytes |
| `double` | 8 bytes |
| `std::string` | 2(len) + N bytes (UTF-8) |
| `SessionIdType` (= `unsigned short`) | 2 bytes |
| `PacketSequence` (= `unsigned long long`) | 8 bytes |

---

## Step 3: 세션 상태 관리

게임 서버에서 플레이어 상태(로그인 여부, 캐릭터 데이터 등)를 세션에 보관하는 패턴.

```cpp
class Player final : public RUDPSession {
public:
    explicit Player(MultiSocketRUDPCore& core);

    // 상태 조회 (읽기 전용, 외부에서 사용)
    const std::string& GetPlayerName() const { return playerName; }
    int GetRoomId() const { return roomId; }
    bool IsLoggedIn() const { return isLoggedIn; }

private:
    void OnConnected()    override;
    void OnDisconnected() override;
    void OnReleased()     override;

    // 패킷 핸들러
    void OnLogin(const LoginReq& packet);
    void OnMove(const MoveReq& packet);

    // 플레이어 상태
    std::string playerName;
    int roomId = -1;
    bool isLoggedIn = false;

    // 게임 데이터
    float posX = 0.0f;
    float posY = 0.0f;
};
```

```cpp
void Player::OnConnected() {
    // UDP 연결만 수립됨, 아직 로그인 패킷은 안 옴
    LOG_DEBUG(std::format("Session {} connected, waiting for login", GetSessionId()));
}

void Player::OnLogin(const LoginReq& packet) {
    // 로그인 검증 (DB 조회는 비동기로)
    if (packet.token.empty()) {
        LOG_ERROR(std::format("Session {} invalid token", GetSessionId()));
        DoDisconnect();
        return;
    }

    playerName = packet.playerName;
    isLoggedIn = true;

    LoginRes res;
    res.success = true;
    res.playerId = GetSessionId();
    SendPacket(res);
}

void Player::OnMove(const MoveReq& packet) {
    if (!isLoggedIn) {
        LOG_ERROR("Move packet without login");
        DoDisconnect();
        return;
    }

    posX = packet.x;
    posY = packet.y;
    // 룸 브로드캐스트...
}

void Player::OnReleased() {
    // 상태 초기화 (다음 연결에서 재사용)
    playerName.clear();
    roomId = -1;
    isLoggedIn = false;
    posX = posY = 0.0f;
}
```

---

## Step 4: 브로드캐스트와 룸 시스템

### 싱글톤 룸 매니저 패턴

```cpp
// RoomManager.h
#pragma once
#include <unordered_map>
#include <set>
#include <shared_mutex>

class MultiSocketRUDPCore;

class RoomManager {
public:
    static RoomManager& GetInstance() {
        static RoomManager inst;
        return inst;
    }

    void Init(MultiSocketRUDPCore* core) { this->core = core; }

    int CreateRoom();
    bool JoinRoom(int roomId, SessionIdType sessionId);
    void LeaveRoom(int roomId, SessionIdType sessionId);
    void BroadcastToRoom(int roomId, IPacket& packet, SessionIdType excludeId = INVALID_SESSION_ID);

private:
    MultiSocketRUDPCore* core = nullptr;

    struct Room {
        std::set<SessionIdType> members;
    };

    std::unordered_map<int, Room> rooms;
    mutable std::shared_mutex roomLock;
    int nextRoomId = 1;
};
```

```cpp
// RoomManager.cpp
void RoomManager::BroadcastToRoom(int roomId, IPacket& packet, SessionIdType excludeId) {
    std::shared_lock lock(roomLock);
    auto it = rooms.find(roomId);
    if (it == rooms.end()) return;

    for (auto sessionId : it->second.members) {
        if (sessionId == excludeId) continue;

        auto* session = core->GetUsingSession(sessionId);
        if (session && session->IsConnected()) {
            session->SendPacket(packet);
            // SendPacket은 thread-safe하므로 Lock 유지 중 호출 가능
        }
    }
}
```

```cpp
// main.cpp에서 초기화
RoomManager::GetInstance().Init(&core);
```

```cpp
// Player.cpp에서 사용
void Player::OnJoinRoom(const JoinRoomReq& packet) {
    roomId = packet.roomId;
    RoomManager::GetInstance().JoinRoom(roomId, GetSessionId());

    // 방에 있는 모든 플레이어에게 알림
    PlayerJoinedPacket notify;
    notify.sessionId = GetSessionId();
    notify.playerName = playerName;
    RoomManager::GetInstance().BroadcastToRoom(roomId, notify, GetSessionId());
}
```

---

## Step 5: 타이머와 게임 루프

[[Ticker]]를 이용해 주기적인 게임 로직 처리.

### 게임 루프 TimerEvent

```cpp
// GameLoop.h
#pragma once
#include "TimerEvent.h"

class MultiSocketRUDPCore;
class RoomManager;

class GameLoopEvent final : public TimerEvent {
public:
    GameLoopEvent(TimerEventId id, TimerEventInterval interval,
                  MultiSocketRUDPCore& core)
        : TimerEvent(id, interval), core(core) {}

private:
    void Fire() override;

    MultiSocketRUDPCore& core;
    uint64_t tickCount = 0;
};
```

```cpp
// GameLoop.cpp
void GameLoopEvent::Fire() {
    ++tickCount;

    // 예: 매 10틱마다 위치 동기화 (16ms × 10 = 160ms)
    if (tickCount % 10 == 0) {
        // 모든 룸의 플레이어 위치 브로드캐스트
        RoomManager::GetInstance().BroadcastPositions();
    }

    // 예: 매 틱마다 AI 업데이트
    AIManager::Update();
}
```

```cpp
// main.cpp에서 등록
auto gameLoop = TimerEventCreator::Create<GameLoopEvent>(16 /*ms*/, core);
Ticker::GetInstance().RegisterTimerEvent(gameLoop);
```

### 세션별 타이머 (특정 플레이어에 대한 딜레이)

```cpp
// 5초 후 해당 플레이어에게 패킷 전송 (간단한 방법)
class DelayedSendEvent final : public TimerEvent {
public:
    DelayedSendEvent(TimerEventId id, TimerEventInterval interval,
                     SessionIdType targetId, MultiSocketRUDPCore& core)
        : TimerEvent(id, interval), targetId(targetId), core(core) {}

private:
    void Fire() override {
        auto* session = core.GetUsingSession(targetId);
        if (session && session->IsConnected()) {
            TimeoutWarningPacket warn;
            session->SendPacket(warn);
        }
        // 1회성 타이머 → 즉시 해제
        Ticker::GetInstance().UnregisterTimerEvent(GetTimerEventId());
    }

    SessionIdType targetId;
    MultiSocketRUDPCore& core;
};

// Player.OnConnected에서 등록
void Player::OnConnected() {
    auto timer = TimerEventCreator::Create<DelayedSendEvent>(
        5000 /*ms*/, GetSessionId(), core);
    Ticker::GetInstance().RegisterTimerEvent(timer);
}
```

---

## Step 6: 전역 변수 접근 패턴

콘텐츠 서버에서 `MultiSocketRUDPCore`나 다른 전역 매니저에 접근하는 패턴.

### 방법 1: Player 멤버에 참조 저장 (권장)

```cpp
class Player : public RUDPSession {
public:
    Player(MultiSocketRUDPCore& core)
        : RUDPSession(core)
        , myCore(core)  // 참조 저장
    {}

private:
    MultiSocketRUDPCore& myCore;

    void OnChat(const ChatReq& packet) {
        // 같은 방의 플레이어 접근
        auto* otherSession = myCore.GetUsingSession(someId);
    }
};
```

### 방법 2: 싱글톤 매니저 (전역 서비스)

```cpp
class GameServer {
public:
    static GameServer& Get() {
        static GameServer inst;
        return inst;
    }

    MultiSocketRUDPCore core{L"MY", L"DevServerCert"};
    RoomManager roomManager;
    AIManager aiManager;
    // ...
};

// Player.cpp
void Player::OnMove(const MoveReq& packet) {
    GameServer::Get().roomManager.BroadcastToRoom(roomId, ...);
}
```

---

## 클라이언트 연동 체크리스트

서버를 구현한 후 클라이언트([[RUDPClientCore]] 또는 C# [[RudpSession_CS]])와 연동 시 확인사항:

```
□ PACKET_CODE / PACKET_KEY 양측 동일 (헤더 코드, XOR 키)
□ PacketId enum 값 양측 동일 → PacketGenerator YAML 기반 자동 생성 권장
□ 패킷 필드 직렬화 순서 양측 동일 (C++: WriteBuffer 순서 = C#: Write 순서)
□ 데이터 타입 크기 양측 동일 (int32_t ↔ int, uint64_t ↔ ulong 등)
□ SessionBroker IP/Port 클라이언트 설정 파일에 정확히 기입
□ Nonce 생성 로직 C++ ↔ C# 동일한가 → [[CryptoSystem]] 참조
□ direction 값: C2S=0, C2S_REPLY=1, S2C=2, S2C_REPLY=3 양측 동일
□ 클라이언트가 HEARTBEAT_REPLY를 정상 반환하는가 (서버 하트비트 응답)
□ sequence=0 ACK 수신 후 클라이언트 isConnected=true 설정되는가
```

---

## 관련 문서
- [[RUDPSession]] — 세션 상속 및 API
- [[PacketGenerator]] — 패킷 코드 자동 생성
- [[PacketFormat]] — 직렬화 상세
- [[Ticker]] — 타이머 이벤트
- [[TroubleShooting]] — 연동 오류 해결
- [[GettingStarted]] — 빠른 시작
