# 세션 서브 컴포넌트 (Session Components)

> **`RUDPSession`을 구성하는 6개 서브 컴포넌트의 전체 멤버, 역할, 상호 관계.**  
> 각 컴포넌트는 독립된 파일로 분리되어 있으며,  
> `RUDPSession`이 이들을 조합해 상태 관리, 암호화, I/O, 흐름 제어를 수행한다.

---

## 목차

1. [전체 구성 개요](#1-전체-구성-개요)
2. [SessionStateMachine](#2-sessionstatemachine)
3. [SessionCryptoContext](#3-sessioncryptocontext)
4. [SessionSocketContext](#4-sessionsocketcontext)
5. [SessionRecvContext (SessionRIOContext 포함)](#5-sessionrecvcontext-sessionriocontext-포함)
6. [SessionSendContext](#6-sessionsendcontext)
7. [RUDPFlowManager](#7-rudpflowmanager)
8. [컴포넌트 간 상호 의존](#8-컴포넌트-간-상호-의존)

---

## 1. 전체 구성 개요

```cpp
class RUDPSession {
    // ─── 6개 서브 컴포넌트 ───────────────────────────────────────────
    SessionStateMachine   stateMachine;      // 세션 상태 (DISCONNECTED/RESERVED/CONNECTED/RELEASING)
    SessionCryptoContext  cryptoContext;     // AES-GCM 키/솔트/핸들
    SessionSocketContext  socketContext;     // 소켓 핸들, 클라이언트 주소, 락
    SessionRIOContext     rioContext;        // RIO 버퍼/컨텍스트 (Recv + Send)
    RUDPFlowManager       flowManager;      // CWND + 수신 윈도우
    SessionPacketOrderer  sessionPacketOrderer;  // 순서 보장 홀딩 큐

    // ─── 스레드 안전 플래그 ──────────────────────────────────────────
    std::atomic_bool nowInReleaseThread{ false };
    std::atomic_bool nowInProcessingRecvPacket{ false };

    // ─── 패킷 핸들러 맵 ──────────────────────────────────────────────
    std::unordered_map<PacketId, PacketHandlerFunc> packetFactoryMap;

    // ─── 시퀀스 추적 ─────────────────────────────────────────────────
    std::atomic<PacketSequence> lastSendPacketSequence{ 0 };
};
```

---

## 2. SessionStateMachine

**파일:** `SessionStateMachine.h / .cpp`

```cpp
class SessionStateMachine {
    std::atomic<SESSION_STATE> state{ SESSION_STATE::DISCONNECTED };

public:
    // ─── 조회 (memory_order_acquire) ─────────────────────────────────
    SESSION_STATE GetSessionState() const noexcept;
    bool IsConnected()    const noexcept;   // state == CONNECTED
    bool IsReserved()     const noexcept;   // state == RESERVED
    bool IsReleasing()    const noexcept;   // state == RELEASING
    bool IsUsingSession() const noexcept;   // RESERVED || CONNECTED
    bool IsDisconnected() const noexcept;   // state == DISCONNECTED

    // ─── 전이 (단순 store) ────────────────────────────────────────────
    void SetReserved();      // any → RESERVED (InitReserveSession에서)
    void SetDisconnected();  // any → DISCONNECTED (Disconnect 완료 후)

    // ─── 전이 (CAS, 경쟁 가능) ────────────────────────────────────────
    bool TryTransitionToConnected();    // RESERVED → CONNECTED
    bool TryTransitionToReleasing();    // RESERVED|CONNECTED → RELEASING (2회 시도)
    bool TryAbortReserved();            // RESERVED → RELEASING (HeartbeatThread 전용)
};
```

**메모리 순서 선택 이유:**

| 연산 | 순서 | 이유 |
|------|------|------|
| 조회 (`IsConnected` 등) | `acquire` | 이전 쓰기가 반영된 최신 값 필요 |
| `SetReserved/Disconnected` | `release` | 이후 읽기가 이 값 이후를 보도록 |
| CAS 전이들 | `acq_rel` | 성공 시 읽기(acquire) + 쓰기(release) 모두 보장 |

→ 상세 CAS 코드: [[SessionLifecycle]] 참조

---

## 3. SessionCryptoContext

**파일:** `SessionCryptoContext.h / .cpp`

```cpp
class SessionCryptoContext {
    unsigned char sessionKey[SESSION_KEY_SIZE];    // AES-128 키 (16 bytes)
    unsigned char sessionSalt[SESSION_SALT_SIZE];  // Nonce 솔트 (16 bytes)
    unsigned char* keyObjectBuffer;                // BCrypt 키 오브젝트 버퍼 (heap)
    BCRYPT_KEY_HANDLE sessionKeyHandle;            // BCryptGenerateSymmetricKey 결과

public:
    void Initialize();
    // → sessionKey/salt 0 초기화
    // → BCryptDestroyKey(sessionKeyHandle)
    // → delete[] keyObjectBuffer

    // 접근자
    const unsigned char* GetSessionKey()  const { return sessionKey;  }
    const unsigned char* GetSessionSalt() const { return sessionSalt; }
    BCRYPT_KEY_HANDLE     GetKeyHandle()  const { return sessionKeyHandle; }

    // 설정자 (RUDPSessionFunctionDelegate를 통해 접근)
    void SetSessionKey(const unsigned char* key);    // copy_n(key, 16, sessionKey)
    void SetSessionSalt(const unsigned char* salt);  // copy_n(salt, 16, sessionSalt)
    void SetKeyObjectBuffer(unsigned char* buf);
    void SetSessionKeyHandle(BCRYPT_KEY_HANDLE handle);
};
```

**`Initialize()` 호출 시점:**

```cpp
// Session Release Thread → Disconnect() → InitializeSession()
void RUDPSession::InitializeSession() {
    cryptoContext.Initialize();     // ← 여기서 키 핸들 파괴 + 버퍼 해제
    flowManager.Reset(0);
    sessionPacketOrderer.Reset();
    lastSendPacketSequence.store(0);
    nowInReleaseThread.store(false);
}
```

**왜 키 핸들을 세션에 저장하는가:**

```
BCryptGenerateSymmetricKey는 비용이 크다 (수백 μs).
세션당 1회 생성 후 재사용하면 패킷마다 생성하는 것보다 훨씬 빠르다.
세션 해제(Initialize) 시 BCryptDestroyKey로 반환.
```

---

## 4. SessionSocketContext

**파일:** `SessionSocketContext.h / .cpp`

```cpp
class SessionSocketContext {
    SOCKET socket = INVALID_SOCKET;
    sockaddr_in clientAddr{};        // 연결된 클라이언트 주소 (TryConnect에서 설정)
    WORD serverPort = 0;             // 이 세션 소켓의 로컬 포트 (bind 후 설정)
    mutable std::shared_mutex socketLock;
    // DoRecv/DoSend: shared_lock (동시 실행 가능)
    // CloseSocket:   unique_lock (독점)

public:
    void SetSocket(SOCKET s) { socket = s; }
    SOCKET GetSocket() const { return socket; }
    void SetClientAddr(const sockaddr_in& addr) { clientAddr = addr; }
    const sockaddr_in& GetClientAddr() const { return clientAddr; }
    void SetServerPort(WORD port) { serverPort = port; }
    WORD GetServerPort() const { return serverPort; }
    std::shared_mutex& GetSocketMutex() const { return socketLock; }

    void CloseSocket() {
        std::unique_lock lock(socketLock);
        if (socket != INVALID_SOCKET) {
            closesocket(socket);
            socket = INVALID_SOCKET;
        }
    }
};
```

**`shared_mutex` 사용 패턴:**

```cpp
// DoRecv / DoSend: 공유 잠금 (동시에 복수 허용)
std::shared_lock lock(session.GetSocketContext().GetSocketMutex());
if (socket == INVALID_SOCKET) return false;
rioFunctionTable.RIOReceiveEx(...);   // 소켓이 열려 있음을 보장

// CloseSocket: 독점 잠금 (DoRecv/DoSend 완료 후 닫기)
std::unique_lock lock(socketLock);
closesocket(socket);
socket = INVALID_SOCKET;
```

**왜 shared_mutex인가:**

```
DoRecv와 DoSend는 동시에 실행될 수 있음 (수신 등록 + 송신 동시)
CloseSocket은 두 작업이 모두 없을 때만 실행해야 함

shared_lock: DoRecv, DoSend가 동시에 획득 가능
unique_lock: CloseSocket이 획득하면 DoRecv, DoSend 모두 블락
→ DoRecv/DoSend 중 소켓이 닫히는 race condition 방지
```

---

## 5. SessionRecvContext (SessionRIOContext 포함)

**파일:** `SessionRIOContext.h / .cpp`

```cpp
class SessionRIOContext {
    SessionRecvContext recvContext;
    SessionSendContext sendContext;
public:
    SessionRecvContext& GetRecvBuffer() { return recvContext; }
    SessionSendContext& GetSendBuffer() { return sendContext; }
    void Cleanup();  // 두 컨텍스트 모두 정리
};

class SessionRecvContext {
    // ─── RIO 버퍼 (세션 수명과 동일) ────────────────────────────────
    char recvBuffer[RECV_BUFFER_SIZE];    // 16KB, page-locked (RIO 등록)
    char clientAddrBuffer[sizeof(SOCKADDR_INET)];  // 28 bytes
    char localAddrBuffer[sizeof(SOCKADDR_INET)];   // 28 bytes

    // ─── RIO 버퍼 ID ─────────────────────────────────────────────────
    RIO_BUFFERID recvBufferId;
    RIO_BUFFERID clientAddrBufferId;
    RIO_BUFFERID localAddrBufferId;

    // ─── RIO_BUF (RIOReceiveEx에 전달하는 슬라이스) ──────────────────
    RIO_BUF recvRIOBuffer;
    RIO_BUF clientAddrRIOBuffer;
    RIO_BUF localAddrRIOBuffer;

    // ─── 수신 완료된 NetBuffer 큐 ────────────────────────────────────
    CListBaseQueue<NetBuffer*> recvBufferList;

    // ─── RIO Request Queue ────────────────────────────────────────────
    RIO_RQ recvRIORQ;

public:
    bool Initialize(const RIO_EXTENSION_FUNCTION_TABLE& rio,
                    SessionIdType sessionId, void* context);
    void Cleanup(const RIO_EXTENSION_FUNCTION_TABLE& rio);

    char* GetRecvBuffer()              { return recvBuffer; }
    RIO_RQ GetRIORQ()                  { return recvRIORQ; }
    RIO_BUF* GetRecvRIOBuffer()        { return &recvRIOBuffer; }
    RIO_BUF* GetClientAddrRIOBuffer()  { return &clientAddrRIOBuffer; }
    RIO_BUF* GetLocalAddrRIOBuffer()   { return &localAddrRIOBuffer; }
    CListBaseQueue<NetBuffer*>& GetRecvBufferList() { return recvBufferList; }
};
```

**RIO 버퍼 3개 등록 이유:**

```
RIOReceiveEx의 파라미터:
  pData        → recvBuffer (실제 UDP 데이터그램)
  pLocalAddressBuffer → localAddrBuffer (로컬 IP/Port, 현재 미사용이나 등록 필요)
  pRemoteAddressBuffer → clientAddrBuffer (클라이언트 IP/Port → CanProcessPacket에서 사용)

세 버퍼 모두 RIO에 등록해야 하므로 세 개의 RIO_BUFFERID가 필요.
```

**`Initialize` 내부:**

```cpp
bool SessionRecvContext::Initialize(
    const RIO_EXTENSION_FUNCTION_TABLE& rio,
    SessionIdType sessionId, void* context)
{
    // ① recv 버퍼 등록 (16KB)
    recvBufferId = rio.RIORegisterBuffer(recvBuffer, RECV_BUFFER_SIZE);
    recvRIOBuffer = { recvBufferId, 0, RECV_BUFFER_SIZE };

    // ② clientAddr 버퍼 등록
    clientAddrBufferId = rio.RIORegisterBuffer(
        clientAddrBuffer, sizeof(SOCKADDR_INET));
    clientAddrRIOBuffer = { clientAddrBufferId, 0, sizeof(SOCKADDR_INET) };

    // ③ localAddr 버퍼 등록
    localAddrBufferId = rio.RIORegisterBuffer(
        localAddrBuffer, sizeof(SOCKADDR_INET));
    localAddrRIOBuffer = { localAddrBufferId, 0, sizeof(SOCKADDR_INET) };

    return recvBufferId  != RIO_INVALID_BUFFERID
        && clientAddrBufferId != RIO_INVALID_BUFFERID
        && localAddrBufferId  != RIO_INVALID_BUFFERID;
}
```

---

## 6. SessionSendContext

```cpp
class SessionSendContext {
    // ─── RIO send 버퍼 (배치 전송용) ────────────────────────────────
    char rioSendBuffer[MAX_SEND_BUFFER_SIZE_BYTES]; // 32KB
    RIO_BUFFERID sendBufferId;

    // ─── 재전송 추적 ─────────────────────────────────────────────────
    std::unordered_map<PacketSequence, SendPacketInfo*> sendPacketInfoMap;
    mutable std::shared_mutex sendPacketInfoMapLock;
    // 읽기(재전송 스레드): shared_lock
    // 쓰기(ACK 수신, 세션 해제): unique_lock

    // ─── I/O 모드 제어 ───────────────────────────────────────────────
    std::atomic<IO_MODE> ioMode{ IO_MODE::IO_NONE_SENDING };
    // IO_NONE_SENDING: 현재 RIO Send 없음
    // IO_SENDING:      RIO Send 진행 중

    // ─── 보류 큐 (흐름 제어) ─────────────────────────────────────────
    std::queue<std::pair<PacketSequence, NetBuffer*>> pendingPacketQueue;
    mutable std::mutex pendingQueueLock;
    bool pendingQueueEmpty = true;  // 빠른 확인용

    // ─── 시퀀스 캐시 (중복 전송 방지) ────────────────────────────────
    std::unordered_set<PacketSequence> cachedSequenceSet;

public:
    char* GetRIOSendBuffer()    { return rioSendBuffer; }
    RIO_BUFFERID GetSendBufferId() { return sendBufferId; }

    IO_MODE GetIOMode() const   { return ioMode.load(memory_order_acquire); }
    bool TrySetIOSending();     // CAS: NONE_SENDING → SENDING
    void SetIONoneSending();    // store: SENDING → NONE_SENDING

    // sendPacketInfoMap 접근
    void InsertSendPacketInfo(PacketSequence seq, SendPacketInfo* info);
    SendPacketInfo* FindAndEraseSendPacketInfo(PacketSequence seq);
    void ForEachAndClearSendPacketInfoMap(
        const std::function<void(SendPacketInfo*)>& func);

    // pendingPacketQueue 접근
    bool PushToPendingQueue(PacketSequence seq, NetBuffer* buf);
    bool PopFromPendingQueue(PacketSequence& seq, NetBuffer*& buf);
    bool IsPendingQueueEmpty() const { return pendingQueueEmpty; }
    std::mutex& GetPendingQueueLock() { return pendingQueueLock; }
};
```

**`cachedSequenceSet` 중복 전송 방지:**

```cpp
// MakeSendStream 내부 (RUDPIOHandler)
for (auto* info : sendPacketInfosToSend) {
    // 이미 이번 배치에 포함됐는가?
    if (!sendContext.cachedSequenceSet.insert(info->sendPacketSequence).second) {
        continue;  // 중복 → 스킵
    }
    // 배치 버퍼에 복사
    memcpy(rioSendBuffer + offset, info->buffer->m_pSerializeBuffer, packetSize);
    offset += packetSize;
}
sendContext.cachedSequenceSet.clear();  // 배치 완료 후 초기화
```

**왜 중복이 발생하는가:**

재전송 스레드가 같은 패킷을 `sendPacketInfoList`에 여러 번 추가하거나,  
ACK 수신 전 `MakeSendStream`이 두 번 호출되는 경우  
같은 sequence의 패킷이 스트림에 두 번 포함될 수 있다.

**`sendPacketInfoMap shared_mutex` 패턴:**

```cpp
// 재전송 스레드 (읽기)
{
    std::shared_lock lock(sendPacketInfoMapLock);
    for (auto& [seq, info] : sendPacketInfoMap) {
        // 타임아웃 확인, 재전송
    }
}

// ACK 수신 (쓰기)
SendPacketInfo* FindAndEraseSendPacketInfo(PacketSequence seq) {
    std::unique_lock lock(sendPacketInfoMapLock);
    auto it = sendPacketInfoMap.find(seq);
    if (it == sendPacketInfoMap.end()) return nullptr;
    auto* info = it->second;
    sendPacketInfoMap.erase(it);
    return info;
}
```

---

## 7. RUDPFlowManager

**파일:** `RUDPFlowManager.h / .cpp`

```cpp
class RUDPFlowManager {
    RUDPFlowController sendController;    // CWND 관리
    RUDPReceiveWindow  receiveWindow;     // 수신 윈도우 관리
    PacketSequence     lastSendAckedSeq;  // 마지막 ACK 시퀀스

public:
    void Reset(PacketSequence initialSeq);

    // 송신 제어
    bool CanSend(PacketSequence nextSeq) const noexcept;
    void OnAckReceived(PacketSequence ackedSeq);
    void OnCongestionEvent() noexcept;
    void OnTimeout() noexcept;

    // 수신 제어
    bool CanAccept(PacketSequence sequence) const noexcept;
    void MarkReceived(PacketSequence sequence);
    uint8_t GetAdvertisableWindow() const noexcept;
};
```

→ 상세 동작: [[FlowController]]

**`Reset` 호출 시점:**

```cpp
// TryConnect() 성공 시
flowManager.Reset(LOGIN_PACKET_SEQUENCE);  // = 0

// Reset 내부:
// sendController.Reset(0):
//   cwnd = INITIAL_CWND, lastReplySequence = 0, inRecovery = false
// receiveWindow.Reset(1, windowSize):
//   windowStart = 1 (seq=0는 CONNECT, 첫 데이터는 1부터)
//   receivedMask.assign(windowSize, false)
// lastSendAckedSeq = 0
```

---

## 8. 컴포넌트 간 상호 의존

```
stateMachine
  → IsConnected(), TryTransitionToReleasing() 등
  → 거의 모든 메서드에서 상태 확인 후 처리

cryptoContext
  → EncodePacket/DecodePacket에서 GetSessionSalt(), GetKeyHandle() 사용

socketContext
  → DoRecv/DoSend: GetSocket() + GetSocketMutex()
  → TryConnect: SetClientAddr()
  → CanProcessPacket: GetClientAddr() 비교

rioContext (RecvContext + SendContext)
  → RecvContext: DoRecv에서 RIO 버퍼 전달
  → SendContext: DoSend, MakeSendStream에서 재전송 맵/IO 모드 관리

flowManager
  → OnRecvPacket: CanAccept(), MarkReceived(), GetAdvertisableWindow()
  → OnSendReply: OnAckReceived(), CanSend()
  → SendPacket: CanSend()

sessionPacketOrderer
  → OnRecvPacket: OnReceive(sequence, buffer, callback)
  → 순서 보장 홀딩 큐 관리
```

**초기화 순서 (InitReserveSession):**

```
1. socketContext.SetSocket(newSocket)
2. socketContext.SetServerPort(port)
3. rioContext.recvContext.Initialize(rioFunctionTable, sessionId, ...)
4. rioContext.sendContext.Initialize(rioFunctionTable)
5. DoRecv() 등록
6. stateMachine.SetReserved()
```

**해제 순서 (Disconnect / AbortReservedSession):**

```
1. socketContext.CloseSocket()        ← 소켓 먼저 닫기
2. sendContext.ForEachAndClearSendPacketInfoMap()  ← SendPacketInfo 정리
3. OnReleased() 콘텐츠 훅
4. InitializeSession()
   ├─ cryptoContext.Initialize()     ← 키 핸들 파괴
   ├─ flowManager.Reset(0)
   ├─ sessionPacketOrderer.Reset()
   └─ lastSendPacketSequence = 0
5. stateMachine.SetDisconnected()
6. unusedSessionIdList.push_back(id)
```

---

## 관련 문서
- [[SessionLifecycle]] — 상태 전이 코드
- [[RUDPSession]] — 컴포넌트 사용처 (SendPacket, OnRecvPacket 등)
- [[FlowController]] — RUDPFlowManager 상세
- [[CryptoHelper]] — SessionCryptoContext에서 사용하는 BCrypt API
- [[RIOManager]] — SessionRecvContext/SendContext RIO 초기화
