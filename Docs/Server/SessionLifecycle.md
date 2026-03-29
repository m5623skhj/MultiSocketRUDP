# 세션 생명주기 (Session Lifecycle)

> **`RUDPSession`의 4가지 상태와 각 전이 조건, 관여 스레드, 코드 경로를 정리한다.**  
> 상태 전이는 모두 CAS(Compare-And-Swap) 또는 store 기반 원자 연산으로 수행된다.

---

## 목차

1. [상태 전이 다이어그램](#1-상태-전이-다이어그램)
2. [상태 정의](#2-상태-정의)
3. [전이 1: DISCONNECTED → RESERVED](#3-전이-1-disconnected--reserved)
4. [전이 2: RESERVED → CONNECTED](#4-전이-2-reserved--connected)
5. [전이 3: RESERVED → RELEASING (타임아웃)](#5-전이-3-reserved--releasing-타임아웃)
6. [전이 4: CONNECTED → RELEASING](#6-전이-4-connected--releasing)
7. [전이 5: RELEASING → DISCONNECTED](#7-전이-5-releasing--disconnected)
8. [SessionStateMachine 구현](#8-sessionstatemachine-구현)
9. [콘텐츠 훅 호출 순서 보장](#9-콘텐츠-훅-호출-순서-보장)
10. [동시성 시나리오](#10-동시성-시나리오)

---

## 1. 상태 전이 다이어그램

![[SessionStateMachine.svg]]

```
           AcquireSession()
           InitReserveSession()
DISCONNECTED ──────────────────► RESERVED
    ▲                               │
    │                   ┌───────────┴──────────────┐
    │            CONNECT 패킷 수신           30초 타임아웃
    │            TryConnect() 성공          AbortReservedSession()
    │                   │                           │
    │                   ▼                           │
    │               CONNECTED                       │
    │                   │                           │
    │         DoDisconnect(reason) 호출             │
    │     (오류/재전송초과/클라이언트종료)           │
    │                   │                           │
    │                   └──────────┬────────────────┘
    │                              ▼
    │                          RELEASING
    │                              │
    │         IO_SENDING 아님 AND  │
    │      nowInProcessingRecvPacket=false
    │                              │
    │                      Disconnect() 호출
    │                    (Release Thread)
    └──────────────────────────────┘
         SetDisconnected() + 풀 반환
```

---

## 2. 상태 정의

```cpp
enum class SESSION_STATE : unsigned char {
    DISCONNECTED = 0,   // 풀에 반환됨, 재사용 대기
    RESERVED     = 1,   // 세션 발급, UDP 수신 대기 중
    CONNECTED    = 2,   // 클라이언트 연결 완료, 송수신 가능
    RELEASING    = 3,   // 해제 진행 중, IO 완료 대기
};
```

| 상태 | `IsUsingSession()` | 소켓 | `GetUsingSession` 접근 |
|------|-------------------|------|------------------------|
| `DISCONNECTED` | `false` | 없음 | ❌ |
| `RESERVED` | `true` | 있음 (수신 대기) | ✅ |
| `CONNECTED` | `true` | 있음 (송수신) | ✅ |
| `RELEASING` | `false` | 있음 (곧 닫힘) | ❌ |

---

## 3. 전이 1: DISCONNECTED → RESERVED

**트리거:** `RUDPSessionBroker::ReserveSession()` — SessionBroker worker thread

**코드 경로:**

```cpp
// 1. 풀에서 세션 할당
RUDPSession* session = sessionManager.AcquireSession();
// → unusedSessionIdList.pop_front()
// → sessionList[id]

// 2. 소켓 + RIO 초기화 (MultiSocketRUDPCore::InitReserveSession)
auto code = InitReserveSession(*session);

// InitReserveSession 내부:
{
    // UDP 소켓 생성
    SOCKET sock = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_REGISTERED_IO);
    bind(sock, INADDR_ANY, 0);
    getsockname() → serverPort;

    // RIO 등록
    rioManager->InitializeSessionRIO(*session, session->GetThreadId());

    // 수신 대기 등록
    ioHandler->DoRecv(*session);

    // ← 여기서 전이
    session->stateMachine.SetReserved();  // store: any → RESERVED
}

// 3. 암호화 키 생성
InitSessionCrypto(*session);

// 4. 세션 정보 전송 (TLS)
SendSessionInfoToClient(...);
```

**전이 후 상태:**
- `reservedTimestamp` 갱신 (30초 타임아웃 카운트 시작)
- UDP 소켓 바인딩 완료, 클라이언트 패킷 수신 대기

---

## 4. 전이 2: RESERVED → CONNECTED

**트리거:** `CONNECT_TYPE` 패킷 수신 — RecvLogic Worker Thread

**코드 경로:**

```cpp
bool RUDPSession::TryConnect(
    NetBuffer& recvPacket,
    const sockaddr_in& clientAddr)
{
    // ① 시퀀스 확인 (반드시 0)
    PacketSequence packetSequence;
    recvPacket >> packetSequence;
    if (packetSequence != LOGIN_PACKET_SEQUENCE) return false;  // = 0

    // ② SessionId 확인 (브로커가 발급한 ID와 일치해야 함)
    SessionIdType recvSessionId;
    recvPacket >> recvSessionId;
    if (recvSessionId != GetSessionId()) return false;

    // ③ 상태 전이 (RESERVED → CONNECTED, CAS)
    if (!stateMachine.TryTransitionToConnected()) return false;
    // → CAS: RESERVED(1) → CONNECTED(2)
    // → 이미 CONNECTED이거나 RELEASING이면 실패

    // ④ 클라이언트 주소 저장
    socketContext.SetClientAddr(clientAddr);

    // ⑤ 흐름 제어 초기화
    flowManager.Reset(LOGIN_PACKET_SEQUENCE);
    // → sendController.Reset(0), receiveWindow.Reset(1, windowSize)

    // ⑥ 콘텐츠 훅
    OnConnected();   // ← 콘텐츠 서버가 구현

    // ⑦ ACK 전송 (sequence=0)
    SendReplyToClient(LOGIN_PACKET_SEQUENCE);

    return true;
}
```

**전이 전 검사 목록:**

| 검사 | 실패 조건 | 의미 |
|------|-----------|------|
| `packetSequence == 0` | 아닌 경우 | 잘못된 연결 요청 |
| `recvSessionId == GetSessionId()` | 불일치 | 다른 세션 ID로 연결 시도 |
| `TryTransitionToConnected()` CAS | 이미 CONNECTED/RELEASING | 중복 연결 요청 |

---

## 5. 전이 3: RESERVED → RELEASING (타임아웃)

**트리거:** HeartbeatThread, 30초 경과

**코드 경로:**

```cpp
// RUDPSession::CheckReservedSessionTimeout (세션 멤버 함수)
bool CheckReservedSessionTimeout(unsigned long long now) const {
    return stateMachine.IsReserved()
        && (now - sessionReservedTime >= RESERVED_SESSION_TIMEOUT_MS);  // 30000ms
}

// RUDPSession::AbortReservedSession (세션 멤버 함수)
void AbortReservedSession() {
    // CAS: RESERVED → RELEASING
    if (!stateMachine.TryAbortReserved()) return;
    // → 이미 CONNECTED이면 실패 → 정상 연결 흐름으로

    const SessionIdType disconnectTargetSessionId = sessionId;
    nowInReleaseThread.store(true, std::memory_order_seq_cst);

    // 소켓 닫기 (RIO Cleanup + socketContext.CloseSocket())
    CloseSocket();

    // 세션 내부 상태 초기화
    // (DoDisconnect 경로와 달리 Session Release Thread를 거치지 않고 즉시 처리)
    // SetDisconnected() 호출 없음 — 다음 AcquireSession() 시 SetReserved()로 덮어씀
    InitializeSession();
    MultiSocketRUDPCoreFunctionDelegate::DisconnectSession(disconnectTargetSessionId);
    // → unusedSessionIdList.push_back(id)
}
```

---

## 6. 전이 4: CONNECTED → RELEASING

**트리거:** `DoDisconnect(reason)` 호출

**호출 원인:**

| 원인 | 호출 위치 |
|------|-----------|
| 콘텐츠 서버 직접 호출 | 핵 감지, 인증 실패 등 |
| 재전송 횟수 초과 | `RunRetransmissionThread` |
| 패킷 처리 실패 | `ProcessPacket` / `OnRecvPacket` 반환 false |
| 클라이언트 `DISCONNECT_TYPE` | `ProcessByPacketType` |
| RIO 오류 | `GetIOCompletedContext` `Status != 0` |

**코드 경로:**

```cpp
void RUDPSession::DoDisconnect(const DISCONNECT_REASON reason)
{
    // ① CAS: RESERVED 또는 CONNECTED → RELEASING
    if (!stateMachine.TryTransitionToReleasing()) return;
    // → RESERVED(1) → RELEASING(3) 먼저 시도
    // → 실패하면 CONNECTED(2) → RELEASING(3) 시도
    // → 둘 다 실패 (이미 RELEASING/DISCONNECTED) → return

    // ② 해제 진행 플래그 설정
    nowInReleaseThread.store(true, std::memory_order_seq_cst);

    // ③ 콘텐츠 훅
    OnDisconnected();   // ← 콘텐츠 서버가 구현

    // ④ Session Release Thread에 알림
    MultiSocketRUDPCoreFunctionDelegate::PushToDisconnectTargetSession(*this);
    // → releaseSessionIdList.push_back(sessionId)
    // → SetEvent(sessionReleaseEventHandle)
}
```

**`TryTransitionToReleasing` CAS 두 번 시도:**

```cpp
bool SessionStateMachine::TryTransitionToReleasing()
{
    // RESERVED → RELEASING 시도
    SESSION_STATE expected = SESSION_STATE::RESERVED;
    if (state.compare_exchange_strong(
            expected, SESSION_STATE::RELEASING,
            std::memory_order_acq_rel)) {
        return true;
    }

    // CONNECTED → RELEASING 시도 (RESERVED가 아니었을 경우)
    expected = SESSION_STATE::CONNECTED;
    return state.compare_exchange_strong(
        expected, SESSION_STATE::RELEASING,
        std::memory_order_acq_rel);
}
```

---

## 7. 전이 5: RELEASING → DISCONNECTED

**트리거:** Session Release Thread (`Disconnect()` 호출)

**안전 확인 조건:**

```cpp
// Session Release Thread 루프
bool isSending    = session.GetSendContext().GetIOMode() == IO_MODE::IO_SENDING;
bool isProcessing = session.nowInProcessingRecvPacket.load(std::memory_order_seq_cst);

if (isSending || isProcessing) {
    remainList.push_back(id);  // 다음 루프에 재시도
    continue;
}

// 안전 확인 완료 → Disconnect
session.Disconnect();
```

**`Disconnect()` 코드 경로:**

```cpp
void RUDPSession::Disconnect()
{
    // ① 소켓 닫기 (unique_lock)
    sessionDelegate.CloseSocket(*this);
    // → unique_lock(socketLock) → closesocket() → INVALID_SOCKET

    // ② 미처리 sendPacketInfo 전체 정리
    rioContext.GetSendContext().ForEachAndClearSendPacketInfoMap([this](SendPacketInfo* info) {
        core.EraseSendPacketInfo(info, threadId);
        // → sendPacketInfoList[threadId]에서 제거
        // → SendPacketInfo::Free(info)
    });

    // ③ 콘텐츠 훅
    OnReleased();   // ← 콘텐츠 서버가 구현

    // ④ 세션 내부 상태 초기화
    InitializeSession();
    // → sessionId = INVALID_SESSION_ID
    // → cryptoContext.Initialize() (키 핸들 파괴)
    // → clientAddr/clientSockAddrInet/sessionReservedTime 초기화
    // → nowInReleaseThread = false
    // → flowManager.Initialize(maximumHoldingPacketQueueSize)
    // → rioContext.GetSendContext().Reset()
    // → sessionPacketOrderer.Initialize(maximumHoldingPacketQueueSize)

    // ⑤ 상태 전이 완료
    stateMachine.SetDisconnected();
    // → store: RELEASING → DISCONNECTED

    // ⑥ 풀 반환
    DisconnectSession(GetSessionId());
    // → connectedUserCount--
    // → unusedSessionIdList.push_back(sessionId)
}
```

**`IO_SENDING` 대기 이유:**

```
RIO Send가 진행 중인 상태에서 소켓을 닫으면:
  → RIO 완료 큐에 에러 상태로 완료됨
  → IOCompleted에서 세션 포인터를 접근하지만 이미 해제됨 → crash

IO_SENDING이 아닐 때 닫으면:
  → 진행 중인 RIO Send 없음 → 안전하게 닫기 가능
```

**`nowInProcessingRecvPacket` 대기 이유:**

```
RecvLogic Worker가 session->OnRecvPacket() 처리 중에 세션이 해제되면:
  → 처리 완료 후 session 멤버 접근 → use-after-free → crash

nowInProcessingRecvPacket=false를 확인 후 해제:
  → RecvLogic Worker가 완전히 빠져나온 상태
```

---

## 8. SessionStateMachine 구현

```cpp
class SessionStateMachine {
    std::atomic<SESSION_STATE> state{ SESSION_STATE::DISCONNECTED };

public:
    // 단순 전이 (경쟁 없음)
    void SetReserved()    { state.store(SESSION_STATE::RESERVED, memory_order_release); }
    void SetDisconnected(){ state.store(SESSION_STATE::DISCONNECTED, memory_order_release); }

    // CAS 전이 (경쟁 가능)
    bool TryTransitionToConnected() {
        SESSION_STATE expected = SESSION_STATE::RESERVED;
        return state.compare_exchange_strong(
            expected, SESSION_STATE::CONNECTED, memory_order_acq_rel);
    }

    bool TryTransitionToReleasing() {
        SESSION_STATE expected = SESSION_STATE::RESERVED;
        if (state.compare_exchange_strong(expected, SESSION_STATE::RELEASING, memory_order_acq_rel))
            return true;
        expected = SESSION_STATE::CONNECTED;
        return state.compare_exchange_strong(expected, SESSION_STATE::RELEASING, memory_order_acq_rel);
    }

    bool TryAbortReserved() {
        SESSION_STATE expected = SESSION_STATE::RESERVED;
        return state.compare_exchange_strong(
            expected, SESSION_STATE::RELEASING, memory_order_acq_rel);
    }

    // 조회
    bool IsConnected()    const { return state.load(memory_order_acquire) == SESSION_STATE::CONNECTED; }
    bool IsReserved()     const { return state.load(memory_order_acquire) == SESSION_STATE::RESERVED; }
    bool IsReleasing()    const { return state.load(memory_order_acquire) == SESSION_STATE::RELEASING; }
    bool IsUsingSession() const {
        auto s = state.load(memory_order_acquire);
        return s == SESSION_STATE::RESERVED || s == SESSION_STATE::CONNECTED;
    }
};
```

---

## 9. 콘텐츠 훅 호출 순서 보장

| 훅 | 호출 시점 | 순서 보장 |
|----|-----------|-----------|
| `OnConnected()` | `TryConnect()` → ACK 전송 **전** | CONNECTED 전이 직후, 단 1회 |
| `OnDisconnected()` | `DoDisconnect()` → `PushToDisconnect` **전** | RELEASING 전이 직후, 단 1회 |
| `OnReleased()` | `Disconnect()` → `InitializeSession()` **전** | `OnDisconnected` 이후 반드시 호출 |

**`OnDisconnected → OnReleased` 순서 보장:**

```
DoDisconnect():
  TryTransitionToReleasing() → OnDisconnected() → PushToDisconnectTargetSession

(Session Release Thread)
  IO 완료 대기 → Disconnect():
    CloseSocket() → OnReleased() → InitializeSession() → SetDisconnected()
```

`OnDisconnected`는 DoDisconnect를 호출한 스레드에서 동기적으로 실행된다.  
`OnReleased`는 항상 Session Release Thread에서 실행된다.  
두 훅이 다른 스레드에서 실행될 수 있으므로 공유 자원 접근 시 주의해야 한다.

---

## 10. 동시성 시나리오

### 시나리오 A: 동시에 두 스레드가 DoDisconnect 호출

```
[Retransmission Thread]    [RecvLogic Worker]
DoDisconnect()              DoDisconnect()
  TryTransitionToReleasing   TryTransitionToReleasing
    CONNECTED → RELEASING       이미 RELEASING → CAS 실패
    (성공)                       return (no-op)
  OnDisconnected()
  PushToDisconnect()
```

**결과:** OnDisconnected는 한 번만 호출됨 (CAS 원자성 보장).

### 시나리오 B: 타임아웃과 CONNECT 경쟁

```
t=29.9s: TryAbortReserved 시작 (HeartbeatThread)
t=30.0s: CONNECT 패킷 도착 (RecvLogic Worker)
  TryTransitionToConnected: RESERVED → CONNECTED (CAS 성공)
t=30.0s: TryAbortReserved: RESERVED → RELEASING
  CAS 실패 (이미 CONNECTED) → return
```

**결과:** 연결이 정상 수립됨. 타임아웃이 무시됨.

### 시나리오 C: CONNECT 직후 DoDisconnect

```
RecvLogic Worker:
  TryTransitionToConnected: RESERVED → CONNECTED (성공)
  OnConnected() 실행 중

다른 스레드:
  DoDisconnect()
    TryTransitionToReleasing: CONNECTED → RELEASING (성공)
    OnDisconnected() 호출

RecvLogic Worker:
  OnConnected() 완료 후 SendReplyToClient
    IsConnected() → false (이미 RELEASING)
    → ACK 전송 시도, nowInReleaseThread 확인
```

**결과:** OnConnected → OnDisconnected 순서 보장됨.  
ACK는 nowInReleaseThread 체크로 전송 시도하지만 효과 없음.

---

## 관련 문서
- [[SessionComponents]] — SessionStateMachine 구현 상세
- [[RUDPSession]] — TryConnect, DoDisconnect, Disconnect 전체 코드
- [[ThreadModel]] — Session Release Thread 대기 조건
- [[RUDPSessionBroker]] — RESERVED 상태 진입 (ReserveSession)
- [[MultiSocketRUDPCore]] — AbortReservedSession, DisconnectSession
