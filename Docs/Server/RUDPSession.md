# RUDPSession

> **클라이언트 연결의 단위 객체이자 콘텐츠 서버의 핵심 확장 지점.**  
> 상태머신, 암호화 컨텍스트, RIO I/O, 흐름 제어, 패킷 순서 보장을 내부에서 처리하며,  
> 콘텐츠 개발자는 이 클래스를 **상속**해 패킷 핸들러와 이벤트 훅만 구현하면 된다.

---

## 목차

1. [콘텐츠 개발자 빠른 시작](#1-콘텐츠-개발자-빠른-시작)
2. [상태 전이 요약](#2-상태-전이-요약)
3. [패킷 핸들러 등록](#3-패킷-핸들러-등록)
4. [이벤트 훅](#4-이벤트-훅)
5. [패킷 송신 API](#5-패킷-송신-api)
6. [수동 연결 해제](#6-수동-연결-해제)
7. [내부 수신 파이프라인](#7-내부-수신-파이프라인)
8. [내부 송신 파이프라인](#8-내부-송신-파이프라인)
9. [흐름 제어와 보류 큐](#9-흐름-제어와-보류-큐)
10. [하트비트 메커니즘](#10-하트비트-메커니즘)
11. [동시성 보호 전략](#11-동시성-보호-전략)
12. [주의사항 및 흔한 실수](#12-주의사항-및-흔한-실수)

---

## 1. 콘텐츠 개발자 빠른 시작

### 최소 구현 예시

```cpp
// Player.h
#pragma once
#include "RUDPSession.h"
#include "Protocol.h"  // PacketGenerator가 생성한 패킷 클래스

class Player final : public RUDPSession {
public:
    explicit Player(MultiSocketRUDPCore& core);
    ~Player() override = default;

protected:
    void OnConnected()    override;
    void OnDisconnected() override;
    void OnReleased()     override;

private:
    // 패킷 핸들러
    void OnPing(const Ping& packet);
    void OnMove(const MoveReq& packet);
    void OnChat(const ChatReq& packet);

    // 게임 데이터
    std::string playerName;
    int roomId = -1;
};
```

```cpp
// Player.cpp
#include "Player.h"
#include "PacketIdType.h"

Player::Player(MultiSocketRUDPCore& core)
    : RUDPSession(core)
{
    // ① 모든 패킷 핸들러를 생성자에서 등록
    RegisterPacketHandler<Player, Ping>(
        static_cast<PacketId>(PACKET_ID::PING), &Player::OnPing);
    RegisterPacketHandler<Player, MoveReq>(
        static_cast<PacketId>(PACKET_ID::MOVE_REQ), &Player::OnMove);
    RegisterPacketHandler<Player, ChatReq>(
        static_cast<PacketId>(PACKET_ID::CHAT_REQ), &Player::OnChat);
}

void Player::OnConnected() {
    // 서버와 UDP 연결이 완전히 수립된 직후 호출
    // DB 조회, 캐릭터 데이터 로드 등
    playerName = "Guest_" + std::to_string(GetSessionId());
    LOG_DEBUG(std::format("Player {} connected", playerName));
}

void Player::OnDisconnected() {
    // RELEASING 전이 직후 호출. 소켓은 아직 열려 있음
    // 게임 룸에서 퇴장 처리, 데이터 저장 등
    if (roomId >= 0) {
        RoomManager::Leave(roomId, GetSessionId());
    }
}

void Player::OnReleased() {
    // 풀 반환 직전. 소켓은 이미 닫혀 있음
    // 멤버 변수 초기화
    playerName.clear();
    roomId = -1;
}

void Player::OnPing(const Ping& /*packet*/) {
    Pong pong;
    SendPacket(pong);  // ← 응답 전송
}

void Player::OnMove(const MoveReq& packet) {
    // packet.x, packet.y 등 역직렬화된 데이터 사용
    MoveRes res;
    res.x = packet.x;
    res.y = packet.y;
    SendPacket(res);
}

void Player::OnChat(const ChatReq& packet) {
    // 채팅 브로드캐스트 등
}
```

```cpp
// 서버 진입점
MultiSocketRUDPCore core(L"MY", L"DevServerCert");
ContentsPacketRegister::Init();  // PacketGenerator 자동 생성 등록 코드 (StartServer 이전)
core.StartServer(
    L"ServerOptionFile/CoreOption.txt",
    L"ServerOptionFile/SessionBrokerOption.txt",
    [](MultiSocketRUDPCore& c) -> RUDPSession* {
        return new Player(c);   // ← 팩토리: Player 인스턴스 반환
    },
    true
);
```

---

## 2. 상태 전이 요약

> 전체 다이어그램과 각 전이 코드: [[SessionLifecycle]]

```
DISCONNECTED ──[AcquireSession]──► RESERVED ──[CONNECT 수신]──► CONNECTED
                                        │                             │
                                 [30초 타임아웃]               [DoDisconnect]
                                        │                             │
                                        └────────────┬────────────────┘
                                                     ▼
                                                 RELEASING
                                                     │
                                          [IO 완료 대기 후]
                                                     ▼
                                              DISCONNECTED (풀 반환)
```

---

## 3. 패킷 핸들러 등록

### `RegisterPacketHandler`

```cpp
template <typename DerivedType, typename PacketType>
void RegisterPacketHandler(
    const PacketId packetId,
    void (DerivedType::* func)(const PacketType&)
);
```

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `packetId` | `PacketId` | 라우팅 키. 보통 `static_cast<PacketId>(PACKET_ID::XXX)` |
| `func` | 멤버 함수 포인터 | `const PacketType&` 하나를 받는 멤버 함수 |

**내부 동작:**

```
람다 저장: packetFactoryMap[packetId] = 
    [func, packetId](RUDPSession* session, NetBuffer* buffer) → std::function<bool()>
    {
        DerivedType* derived = static_cast<DerivedType*>(session);
        shared_ptr<IPacket> packet = BufferToPacket(*buffer, packetId);
        if (packet == nullptr) return []() { return false; };
        return [derived, func, packet]() {
            (derived->*func)(static_cast<PacketType&>(*packet));
            return true;
        };
    };
```

**역직렬화 흐름:**

```
NetBuffer → PacketManager::MakePacket(packetId)
              → shared_ptr<PacketType> 생성
              → packet->BufferToPacket(buffer)
                   → SetBufferToParameters(buffer, field1, field2, ...)
              → func(static_cast<PacketType&>(*packet)) 호출
```

### 등록하지 않은 PacketId 수신 시

```cpp
bool RUDPSession::ProcessPacket(NetBuffer& recvPacket, PacketSequence recvPacketSequence)
{
    PacketId packetId;
    recvPacket >> packetId;

    auto itor = packetFactoryMap.find(packetId);
    if (itor == packetFactoryMap.end()) {
        LOG_ERROR(std::format("Received unknown packet. packetId: {}", packetId));
        return false;  // ← false 반환 → session->DoDisconnect() 호출됨
    }
    // ...
}
```

> **주의:** 등록되지 않은 `PacketId`가 수신되면 세션이 강제 종료된다.  
> 클라이언트와 서버의 `PacketId` 정의는 반드시 동기화되어야 한다.  
> [[PacketGenerator]]를 사용하면 양측을 동시에 업데이트할 수 있다.

### PacketType 작성 요구사항

`RegisterPacketHandler`에 사용하는 패킷 타입은 반드시 `IPacket`을 상속하고  
`BufferToPacket(NetBuffer&)` + `PacketToBuffer(NetBuffer&)`를 구현해야 한다.

```cpp
// Protocol.h (PacketGenerator 자동 생성)
class MoveReq final : public IPacket {
public:
    [[nodiscard]] PacketId GetPacketId() const override;
    void BufferToPacket(NetBuffer& buffer) override;  // 역직렬화
    void PacketToBuffer(NetBuffer& buffer) override;  // 직렬화
public:
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};
```

---

## 4. 이벤트 훅

| 훅 | 호출 스레드 | 호출 시점 | 소켓 상태 | 권장 작업 |
|----|------------|-----------|-----------|-----------|
| `OnConnected()` | RecvLogic Worker | `TryConnect()` 성공, ACK 전송 직전 | 열림 | DB 조회, 세션 초기화 |
| `OnDisconnected()` | `DoDisconnect()` 호출 스레드 | RELEASING 전이 직후 | 열림 | 룸 퇴장, 데이터 저장 |
| `OnReleased()` | Session Release Thread | 풀 반환 직전 | 닫힘 | 멤버 변수 초기화 |

### `OnConnected` 주의사항

```cpp
void Player::OnConnected() {
    // ✅ 안전: SendPacket 호출 가능 (소켓 열림)
    WelcomePacket welcome;
    welcome.serverId = 1;
    SendPacket(welcome);

    // ✅ 안전: GetSessionId() 사용 가능
    LOG_DEBUG(std::format("Session {} connected", GetSessionId()));

    // ⚠️ 주의: 오래 걸리는 동기 I/O는 금물 (RecvLogic Worker를 블락함)
    // DB 조회가 필요하면 비동기로 처리하거나 별도 스레드에서 수행
}
```

### `OnDisconnected` 주의사항

```cpp
void Player::OnDisconnected() {
    // ✅ 안전: 멤버 변수 읽기
    LOG_DEBUG(std::format("Player {} disconnected. Room: {}", playerName, roomId));

    // ✅ 안전: 다른 세션에 알림 (SendPacket 아님. 세션 포인터를 통한 직접 전송은 위험)
    // RoomManager::Broadcast(roomId, LeavePacket{GetSessionId()});

    // ❌ 위험: SendPacket 호출
    // → DoDisconnect()는 RELEASING 전이 후 호출됨
    // → 이미 nowInReleaseThread = true이므로 MultiSocketRUDPCore::SendPacket이 false 반환
    // → 실제 전송이 안 되고 리소스만 낭비됨
}
```

### `OnReleased` 주의사항

```cpp
void Player::OnReleased() {
    // ✅ 안전: 멤버 변수 초기화
    playerName.clear();
    roomId = -1;

    // ❌ 절대 금지: 멤버 변수 외 어떤 참조도 사용하지 말 것
    // → 이 시점에 다른 스레드가 이 세션을 AcquireSession()으로 가져갈 수 있음
    // → InitializeSession() 이후에 호출되므로 sessionId, clientAddr 등이 초기화됨
}
```

---

## 5. 패킷 송신 API

### 5-1. `SendPacket(IPacket&)` — 데이터 패킷 전송 (주 사용 API)

```cpp
bool SendPacket(IPacket& packet);
```

**전제 조건:**
- `IsConnected() == true` (CONNECTED 상태)
- 흐름 제어가 허용하는 경우 즉시 전송, 아니면 PendingQueue에 대기

**반환값:**

| 반환값 | 의미 | 콘텐츠 대응 |
|--------|------|-------------|
| `true` | RIO Send 예약 또는 PendingQueue 보관 성공 | 정상 |
| `false` | 세션이 CONNECTED 상태 아님, 또는 PendingQueue 가득 참 | 이미 `DoDisconnect()` 호출됨 |

**내부 실행 순서:**

```
1. IsConnected() 확인 → false이면 즉시 return false

2. ++lastSendPacketSequence (atomic)

3. NetBuffer 직렬화:
   buffer << SEND_TYPE << packetSequence << packet.GetPacketId()
   packet.PacketToBuffer(buffer)

4. SendPacket(buffer, sequence, isReplyType=false, isCorePacket=false)
   ├─ [흐름 제어 확인]
   │   scoped_lock(pendingQueueLock)
   │   if !pendingQueueEmpty || !flowManager.CanSend(sequence):
   │       pendingPacketQueue.push({sequence, &buffer})
   │       return true (대기)
   │
   └─ SendPacketImmediate(buffer, sequence, false, false)
       ├─ sendPacketInfoPool->Alloc()
       ├─ sendPacketInfo.Initialize(this, &buffer, sequence, false)
       ├─ InsertSendPacketInfo(sequence, sendPacketInfo)  ← 재전송 맵
       ├─ EncodePacket(AES-GCM, SERVER_TO_CLIENT, ...)
       └─ core.SendPacket(sendPacketInfo)
           └─ RUDPIOHandler::DoSend()
```

**사용 예시:**

```cpp
void Player::OnPing(const Ping& packet) {
    Pong pong;
    pong.timestamp = packet.timestamp;  // 에코

    bool ok = SendPacket(pong);
    if (!ok) {
        // false는 세션이 이미 해제 중임을 의미
        // DoDisconnect()가 이미 호출됐으므로 추가 처리 불필요
        return;
    }
}
```

**브로드캐스트 패턴:**

```cpp
// 같은 방의 모든 플레이어에게 전송
void Room::Broadcast(IPacket& packet, SessionIdType excludeId) {
    for (auto sessionId : memberIds) {
        if (sessionId == excludeId) continue;
        auto* player = static_cast<Player*>(
            core.GetUsingSession(sessionId));
        if (player && player->IsConnected()) {
            player->SendPacket(packet);
        }
    }
}
```

---

### 5-2. 내부 전용 송신 메서드들

콘텐츠 레이어에서 직접 호출하는 경우는 거의 없지만, 동작 이해를 위해 설명한다.

#### `SendPacket(NetBuffer&, PacketSequence, bool isReplyType, bool isCorePacket)` — protected

```cpp
bool SendPacket(NetBuffer& buffer, PacketSequence inSendPacketSequence,
                bool isReplyType, bool isCorePacket);
```

| `isReplyType` | `isCorePacket` | 사용 예 | 재전송 추적 |
|---------------|----------------|---------|------------|
| `false` | `false` | 일반 데이터 패킷 | ✅ |
| `true` | `true` | ACK, Heartbeat Reply | ❌ |
| `false` | `true` | Heartbeat | ✅ |

`isReplyType = true`이면 PendingQueue 검사를 건너뛰고 즉시 `SendPacketImmediate`로 이동.  
`isReplyType = false`이면 흐름 제어 확인 후 PendingQueue에 보관하거나 즉시 전송.

#### `SendPacketImmediate` — 즉시 전송 (PendingQueue 우회)

```cpp
bool SendPacketImmediate(NetBuffer& buffer, PacketSequence sequence,
                         bool isReplyType, bool isCorePacket);
```

1. `sendPacketInfoPool->Alloc()` — TLS 메모리 풀에서 할당 (lock-free)
2. `sendPacketInfo.Initialize(...)` — 소유자, 버퍼, 시퀀스, 재전송 여부 설정
3. `isReplyType=false`이면 → `InsertSendPacketInfo(sequence, info)` (재전송 맵 등록)
4. `buffer.m_bIsEncoded == false`이면 → `PacketCryptoHelper::EncodePacket(...)` (AES-GCM)
5. `core.SendPacket(sendPacketInfo)` → RIO Send 큐 삽입

**실패 처리:**

```cpp
if (!core.SendPacket(sendPacketInfo)) {
    if (!isReplyType) {
        // 재전송 맵에서 제거 + EraseSendPacketInfo
        core.EraseSendPacketInfo(sendPacketInfo, threadId);
        rioContext.GetSendContext().EraseSendPacketInfo(inSendPacketSequence);
    } else {
        // Reply 패킷은 재전송 맵에 없으므로 그냥 Free
        sendPacketInfo->isErasedPacketInfo.store(true);
        SendPacketInfo::Free(sendPacketInfo);
    }
    return false;
}
```

---

## 6. 수동 연결 해제

### `DoDisconnect()` — 연결 해제 시작

```cpp
void DoDisconnect();
```

**콘텐츠 서버에서 직접 호출 가능.** 예: 핵 감지, 룰 위반, 인증 실패 등.

```cpp
void Player::OnMove(const MoveReq& packet) {
    // 패킷 검증
    if (!IsValidPosition(packet.x, packet.y)) {
        LOG_ERROR(std::format("Player {} sent invalid position: ({}, {})",
            GetSessionId(), packet.x, packet.y));
        DoDisconnect();   // ← 바로 해제 요청
        return;
    }
    // 정상 처리...
}
```

**중복 호출 안전:** `TryTransitionToReleasing()`이 CAS이므로 여러 스레드에서  
동시에 호출해도 단 하나만 성공하고 나머지는 no-op.

```cpp
void RUDPSession::DoDisconnect()
{
    // 이미 RELEASING/DISCONNECTED 상태이면 즉시 return
    if (!stateMachine.TryTransitionToReleasing()) return;

    nowInReleaseThread.store(true, std::memory_order_seq_cst);
    OnDisconnected();   // ← 콘텐츠 훅

    MultiSocketRUDPCoreFunctionDelegate::PushToDisconnectTargetSession(*this);
}
```

**Disconnect vs DoDisconnect:**

| | `DoDisconnect()` | `Disconnect()` |
|--|------------------|----------------|
| 호출 주체 | 콘텐츠 서버, 패킷 오류, 재전송 초과 | Session Release Thread 전용 |
| 역할 | RELEASING 전이 요청 | 실제 소켓 닫기 + 풀 반환 |
| 소켓 상태 | 아직 열려 있음 | 이 함수 내에서 닫음 |
| 직접 호출 가능? | ✅ | ❌ (내부 전용) |

---

## 7. 내부 수신 파이프라인

> 수신 전체 흐름: [[PacketProcessing]]

### `OnRecvPacket` (RecvLogic Worker Thread에서 호출)

```cpp
bool RUDPSession::OnRecvPacket(NetBuffer& recvPacket);
```

```
1. packetSequence 추출 (buffer >> packetSequence)

2. flowManager.CanAccept(sequence)
   → RUDPReceiveWindow::CanReceive()
   → diff = sequence - windowStart
   → 0 <= diff < windowSize이면 true
   → false이면 패킷 폐기 (로그 없음, 정상적인 중복/범위 외 패킷)

3. SessionPacketOrderer::OnReceive(sequence, buffer, callback)

   결과: PROCESSED        → 정상 처리
   결과: DUPLICATED_RECV  → 재ACK 전송 (SendReplyToClient)
   결과: PACKET_HELD      → HoldingQueue에 보관, ACK 없음
   결과: ERROR_OCCURED    → false 반환 → 호출자가 DoDisconnect() 호출
```

### `ProcessPacket` — 실제 역직렬화 및 핸들러 호출

```cpp
bool RUDPSession::ProcessPacket(NetBuffer& recvPacket, PacketSequence recvPacketSequence)
{
    PacketId packetId;
    recvPacket >> packetId;  // 4바이트 읽기

    auto itor = packetFactoryMap.find(packetId);
    if (itor == packetFactoryMap.end()) {
        LOG_ERROR(std::format("Unknown packet. packetId: {}", packetId));
        return false;  // → DoDisconnect()
    }

    // 람다: 역직렬화 + 핸들러 호출
    if (!itor->second(this, &recvPacket)()) {
        LOG_ERROR(std::format("Failed to process packet. packetId: {}", packetId));
        return false;  // → DoDisconnect()
    }

    flowManager.MarkReceived(recvPacketSequence);  // 수신 윈도우 업데이트
    SendReplyToClient(recvPacketSequence);          // ACK 전송
    return true;
}
```

### `SendReplyToClient` — ACK 전송

```cpp
void RUDPSession::SendReplyToClient(PacketSequence recvPacketSequence)
{
    NetBuffer* buffer = NetBuffer::Alloc();

    auto packetType = PACKET_TYPE::SEND_REPLY_TYPE;
    BYTE advertiseWindow = flowManager.GetAdvertisableWindow();
    *buffer << packetType << recvPacketSequence << advertiseWindow;

    // isReplyType=true → PendingQueue 우회, 재전송 추적 없음
    if (!SendPacket(*buffer, recvPacketSequence, /*isReplyType*/true, /*isCorePacket*/true)) {
        DoDisconnect();
    }
}
```

**advertiseWindow:** `windowSize - usedCount` = 현재 비어 있는 수신 윈도우 칸 수.  
클라이언트는 이 값을 보고 전송 속도를 조절한다. → [[FlowController]] 참조

---

## 8. 내부 송신 파이프라인

### ACK 수신 처리 — `OnSendReply`

```cpp
void RUDPSession::OnSendReply(NetBuffer& recvPacket)
```

클라이언트로부터 `SEND_REPLY_TYPE` 패킷 수신 시 호출된다.

```
1. packetSequence 추출

2. lastSendPacketSequence < sequence → 유효하지 않은 미래 ACK, 무시

3. FindAndEraseSendPacketInfo(sequence)
   → sendPacketInfoMap에서 제거 (읽기 → unique_lock으로 업그레이드)
   → nullptr이면 이미 제거됨 (중복 ACK), return

4. flowManager.OnAckReceived(sequence)
   → RUDPFlowController::OnReplyReceived()
   → gap = sequence - lastReplySequence - 1
   → gap >= 5이면 OnCongestionEvent() (CWND /= 2)
   → 정상이면 cwnd = min(cwnd + 1, MAX_CWND)

5. core.EraseSendPacketInfo(sendPacketInfo, threadId)
   → sendPacketInfoList[threadId]에서 이터레이터로 O(1) 제거
   → SendPacketInfo::Free()

6. TryFlushPendingQueue()
   → 흐름 제어가 허용하면 PendingQueue에서 꺼내 전송
```

### `TryFlushPendingQueue` — 보류 큐 처리

```cpp
void RUDPSession::TryFlushPendingQueue()
```

```cpp
{
    scoped_lock lock(rioContext.GetSendContext().GetPendingQueueLock());
    while (!pendingQueueEmpty) {
        auto& [sequence, _] = pendingQueueFront();
        if (!flowManager.CanSend(sequence)) break;  // 윈도우 초과

        pair<PacketSequence, NetBuffer*> item;
        pendingQueue.pop(item);
        sendBuffers.push_back(item);  // Lock 밖에서 전송
    }
}

// Lock 해제 후 전송 (Lock 보유 시간 최소화)
for (auto& [seq, buf] : sendBuffers) {
    if (!SendPacketImmediate(*buf, seq, false, false)) {
        DoDisconnect();
        // 나머지 버퍼 Free
        break;
    }
}
```

---

## 9. 흐름 제어와 보류 큐

> 상세 동작: [[FlowController]]

### 전송 측 (Server → Client)

**CWND(혼잡 윈도우):** 한 번에 ACK 없이 전송 가능한 패킷 수.

```
초기값: INITIAL_CWND
ACK 수신마다: cwnd = min(cwnd + 1, MAX_CWND)   // AIMD 증가
패킷 유실 감지(gap >= 5): cwnd = max(cwnd / 2, 1)  // AIMD 감소
재전송 타임아웃: cwnd = 1
```

**PendingQueue 조건:**

```cpp
// SendPacket 내부
scoped_lock lock(pendingQueueLock);
bool shouldPend = !pendingQueueEmpty               // 앞에 이미 대기 중인 것이 있거나
               || !flowManager.CanSend(sequence);  // CWND 초과

if (shouldPend) {
    pendingPacketQueue.push({sequence, &buffer});
    return true;
}
```

> **왜 PendingQueue가 꽉 차면 false를 반환하는가?**  
> `RingBuffer<pair<...>>` 고정 크기 큐이므로 `Push`가 실패하면  
> 클라이언트가 너무 느려서 서버가 보내는 속도를 따라가지 못한다는 신호.  
> 이 경우 더 이상 패킷을 보낼 수 없으므로 `DoDisconnect()` 호출이 적절하다.

### 수신 측 (Client → Server)

**RecvWindow(슬라이딩 수신 윈도우):**

```
windowStart ~ windowStart+windowSize-1 범위의 시퀀스만 수신 허용
MarkReceived()로 수신 마킹 + 앞부분 슬라이딩
GetAdvertisableWindow() = windowSize - usedCount → ACK에 포함
```

---

## 10. 하트비트 메커니즘

### 서버 → 클라이언트 (HeartbeatThread)

```cpp
void RUDPSession::SendHeartbeatPacket()
{
    if (nowInReleaseThread.load(acquire) || !IsConnected()) return;

    // CWND 확인 (데이터 패킷과 동일한 흐름 제어 적용)
    PacketSequence nextSeq = lastSendPacketSequence.load() + 1;
    if (!flowManager.CanSend(nextSeq)) return;  // 윈도우 가득 차면 생략

    NetBuffer* buffer = NetBuffer::Alloc();
    auto type = PACKET_TYPE::HEARTBEAT_TYPE;
    PacketSequence seq = IncrementLastSendPacketSequence();
    *buffer << type << seq;

    // isCorePacket=true, isReplyType=false → 재전송 추적됨
    if (!SendPacket(*buffer, seq, false, true)) {
        DoDisconnect();
    }
}
```

### 클라이언트 → 서버 (HEARTBEAT_REPLY)

클라이언트는 `HEARTBEAT_TYPE` 수신 시 즉시 `HEARTBEAT_REPLY_TYPE`으로 응답한다.  
서버는 이를 `SEND_REPLY_TYPE`과 동일하게 처리 → `OnSendReply(sequence)` 호출.

**하트비트가 재전송 추적되는 이유:**  
하트비트 응답(Reply)이 와야 CWND가 유지된다.  
응답 없이 재전송 한계 초과 시 세션 강제 종료 → 클라이언트 연결 상태 감지.

**옵션 파일 관련 설정:**

| 설정 | 섹션 | 권장값 | 설명 |
|------|------|--------|------|
| `HEARTBEAT_THREAD_SLEEP_MS` | `[CORE]` | 3000 | 하트비트 전송 주기 (ms) |
| `MAX_PACKET_RETRANSMISSION_COUNT` | `[CORE]` | 10~20 | 재전송 한계 (하트비트 포함) |
| `RETRANSMISSION_MS` | `[CORE]` | 200 | 재전송 트리거 간격 (ms) |

---

## 11. 동시성 보호 전략

| 데이터/자원 | 보호 수단 | 접근 스레드 | 패턴 |
|------------|-----------|-------------|------|
| 세션 상태 | `atomic<SESSION_STATE>` + CAS | IO/Logic/Heartbeat 모두 | lock-free |
| 소켓 | `shared_mutex socketLock` | DoRecv/DoSend → shared; CloseSocket → unique | 읽기 공유, 닫기 독점 |
| 해제 진행 여부 | `atomic_bool nowInReleaseThread` | 모든 스레드 | seq_cst 메모리 순서 |
| 패킷 처리 여부 | `atomic_bool nowInProcessingRecvPacket` | Logic Worker ↔ Release Thread | seq_cst |
| 재전송 맵 | `shared_mutex sendPacketInfoMapLock` | Logic(write) ↔ Retransmission(read) | 읽기 공유, 갱신 독점 |
| 전송 큐 | `mutex sendPacketInfoQueueLock` | Logic Worker 단독 | 단순 뮤텍스 |
| 보류 큐 | `mutex pendingPacketQueueLock` | Logic Worker 단독 | 단순 뮤텍스 |
| IO 모드 | `atomic<IO_MODE>` + InterlockedCAS | IO Worker ↔ Logic Worker | lock-free SpinLock |

### IO_SENDING 플래그 설계 의도

```
// DoSend의 SpinLock 패턴
while (true) {
    // CAS: IO_NONE_SENDING(0) → IO_SENDING(1)
    if (InterlockedCompareExchange(&ioMode, IO_SENDING, IO_NONE_SENDING) 실패)
        // 다른 스레드가 Send 중 → 재시도 or 종료
        continue / break;

    // 이 시점에서만 RIOSendEx 호출
    TryRIOSend(session, context);
    // IO_SENDING → SendIOCompleted에서 IO_NONE_SENDING으로 복원
    break;
}
```

**한 세션에 동시에 하나의 RIO Send만 허용하는 이유:**  
RIO Send 완료 큐는 순서를 보장하지 않으며, 같은 세션에 복수의 Send가 동시에  
진행되면 데이터 순서가 뒤바뀔 수 있다. SpinLock으로 직렬화한다.

---

## 12. 주의사항 및 흔한 실수

### ❌ 잘못된 패턴들

**1. OnDisconnected에서 SendPacket 호출**
```cpp
void Player::OnDisconnected() {
    GoodbyePacket bye;
    SendPacket(bye);  // ❌ 이 시점에 nowInReleaseThread=true
                      //    core.SendPacket()이 false 반환하며 전송 안 됨
}
```

**2. OnReleased 이후 세션 포인터 보관**
```cpp
// 어딘가에 Player* 저장 후 OnReleased 이후 사용
void GameRoom::AddPlayer(Player* player) {
    members.push_back(player);  // ❌ 풀 반환 후 이 포인터는 재사용됨
}
// 대신 SessionIdType을 저장하고 GetUsingSession()으로 접근할 것
```

**3. 핸들러 내 장시간 동기 블락**
```cpp
void Player::OnLogin(const LoginReq& packet) {
    auto result = db.SyncQuery(...);  // ❌ RecvLogic Worker를 장시간 블락
    // → 같은 스레드에 배정된 다른 세션들의 패킷 처리도 지연됨
}
// 해결: 비동기 DB 또는 별도 스레드풀에서 처리
```

**4. 생성자에서 virtual 함수 호출**
```cpp
Player::Player(MultiSocketRUDPCore& core) : RUDPSession(core) {
    OnConnected();  // ❌ 세션이 아직 DISCONNECTED 상태
                    //    Player의 OnConnected가 호출되지 않고 RUDPSession의 것이 호출됨
}
```

**5. PacketId 불일치**
```cpp
// 서버에서 등록
RegisterPacketHandler<Player, MoveReq>(
    static_cast<PacketId>(PACKET_ID::MOVE_REQ),  // 서버: MOVE_REQ = 5
    &Player::OnMove);

// 클라이언트에서 전송 (C#)
sendBuffer << (uint)PacketId.MoveReq;  // 클라이언트: MoveReq = 6  ← 불일치!
// → 서버가 알 수 없는 PacketId 수신 → DoDisconnect()
```

> **해결**: [[PacketGenerator]]를 사용해 서버/클라이언트의 `PacketId` 정의를 단일 YAML에서 동기화.

---

## 관련 문서
- [[SessionLifecycle]] — 상태 전이 다이어그램과 전이 코드 상세
- [[SessionComponents]] — 서브 컴포넌트(StateMachine, CryptoContext 등) 상세
- [[PacketProcessing]] — 수신 파이프라인 전체 흐름
- [[FlowController]] — CWND와 수신 윈도우 동작 원리
- [[CryptoSystem]] — AES-GCM 암호화 적용 방식
- [[MultiSocketRUDPCore]] — 세션 풀 관리와 서버 생명주기
- [[PacketGenerator]] — 패킷 클래스 자동 생성
- [[GettingStarted]] — 콘텐츠 서버 처음부터 구현하기
