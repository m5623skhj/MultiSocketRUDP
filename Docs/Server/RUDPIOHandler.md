# RUDPIOHandler

> **RIO 기반 비동기 I/O의 실행과 완료 처리를 담당.**  
> `DoRecv`, `DoSend`, `IOCompleted` 세 핵심 메서드로 수신·송신·완료를 관리하며,  
> 복수의 `SendPacketInfo`를 하나의 RIO Send 스트림으로 묶는 배치 전송을 수행한다.

---

## 목차

1. [위치와 의존 관계](#1-위치와-의존-관계)
2. [함수 설명](#2-함수-설명)
3. [DoRecv — 수신 등록](#3-dorecv-수신-등록)
4. [DoSend — 송신 시도 (SpinLock 방식)](#4-dosend-송신-시도-spinlock-방식)
5. [MakeSendContext / MakeSendStream](#5-makesendcontext-makesendstream)
6. [ReservedSendPacketInfoToStream / StoredSendPacketInfoToStream](#6-reservedsendpacketinfotostream-storedsendpacketinfotostream)
7. [IOCompleted — 완료 처리 분기](#7-iocompleted-완료-처리-분기)
8. [RecvIOCompleted — 수신 완료](#8-recviocompleted-수신-완료)
9. [SendIOCompleted — 송신 완료](#9-sendiocompleted-송신-완료)
10. [RefreshRetransmissionSendPacketInfo](#10-refreshretransmissionsendpacketinfo)

---

## 1. 위치와 의존 관계

```
RUDPIOHandler
 ├── IRIOManager&           ← RIOReceiveEx / RIOSend 호출
 ├── ISessionDelegate&      ← 세션 내부 접근 (shared_ptr 없이 인터페이스)
 ├── CTLSMemoryPool<IOContext>&  ← Send IOContext TLS 풀 (lock-free)
 ├── vector<unique_ptr<RetransmissionScheduler>>& retransmissionSchedulers[N]
 │    └── thread별 재전송 heap, waitable timer, wake event
 └── DatagramLossSimulator(optional) ← 테스트용 송수신 datagram 손실 시뮬레이션
```

---

## 2. 함수 설명

### 공개 함수

#### `RUDPIOHandler(IRIOManager& inRioManager, ISessionDelegate& inSessionDelegate, CTLSMemoryPool<IOContext>& contextPool, std::vector<std::unique_ptr<RetransmissionScheduler>>& retransmissionSchedulers, BYTE inMaxHoldingPacketQueueSize, unsigned int inRetransmissionMs, unsigned int inSimulatedPacketLossPercent, int inSimulatedPacketLossSeed)`
- RIO 매니저, 세션 delegate, IOContext 풀, 스레드별 재전송 scheduler를 주입받는다.
- 재전송 기본 주기와 테스트용 datagram 손실 시뮬레이션 설정도 생성 시점에 고정된다.

### `IOCompleted`

```cpp
[[nodiscard]]
bool IOCompleted(IOContext* context, ULONG transferred, BYTE threadId, LONG status = 0) const override;
```

IO Worker에서 호출되는 완료 분기 진입점이다.
요청 당시 generation과 현재 세대를 비교한 뒤 `context->ioType`에 따라 RECV와 SEND 완료 처리를 분기한다. 오류·취소 completion도 이 함수에서 수명 정리를 수행한다.

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `context` | `IOContext*` | 완료된 IO 컨텍스트 |
| `transferred` | `ULONG` | 전송된 바이트 수 |
| `threadId` | `BYTE` | 호출 스레드 ID |
| `status` | `LONG` | IO 완료 상태 (기본값: 0) |

**반환값**
작업 성공 여부를 반환한다. 반환값을 무시하면 컴파일 경고가 발생한다. 호출 측에서 반드시 검사해야 한다.
### `DoRecv`

```cpp
[[nodiscard]]
bool DoRecv(RUDPSession& session) const override;
```

세션에 다음 RIO Receive를 등록한다.
#### `bool DoSend(RUDPSession& session, ThreadIdType threadId) const`
- 세션 송신 시도 진입점이다.
- 세션 단위 `ioMode` CAS로 동시 송신을 직렬화한다.

### 비공개 함수

### `IOCompleted`

```cpp
[[nodiscard]]
bool IOCompleted(IOContext* context, ULONG transferred, BYTE threadId, LONG status = 0) const override;
```

I/O 작업 완료를 처리한다. 완료된 데이터를 처리하고 필요한 경우 다음 I/O 작업을 재등록한다.

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `context` | `IOContext*` | 완료된 I/O 작업의 컨텍스트 |
| `transferred` | `ULONG` | 전송된 바이트 수 |
| `threadId` | `BYTE` | 호출 스레드 ID |
| `status` | `LONG` | [선택] I/O 작업 상태 코드 (기본값: 0) |

**반환값**: 작업 처리 성공 여부를 반환한다. 반환값을 무시하면 컴파일 경고가 발생한다. 호출 측에서 반드시 검사해야 한다.
### `IOCompleted`

```cpp
[[nodiscard]]
bool IOCompleted(IOContext* context, ULONG transferred, BYTE threadId, LONG status = 0) const override;
```

I/O 완료 이벤트를 처리한다. 송신 완료 후 `IO_NONE_SENDING`을 복구하고 후속 송신을 이어간다.

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `context` | `IOContext*` | 완료된 I/O 컨텍스트 |
| `transferred` | `ULONG` | 전송된 바이트 수 |
| `threadId` | `BYTE` | 처리 스레드 ID |
| `status` | `LONG` | I/O 작업 결과 상태 코드 (기본값 0) |

**반환값**: 처리 성공 시 `true`, 실패 시 `false`를 반환한다. 반환값을 무시하면 컴파일 경고가 발생한다. 호출 측에서 반드시 검사해야 한다.
#### `bool TryRIOSend(RUDPSession& session, IOContext* context) const`
- 준비된 send context를 실제 RIO Send 호출로 연결한다.

#### `std::pair<bool, IOContext*> MakeSendContext(RUDPSession& session, ThreadIdType threadId) const`
- 이번 송신에 사용할 `IOContext`를 만들고 전송 컨텍스트를 준비한다.

#### `std::pair<bool, unsigned int> MakeSendStream(RUDPSession& session, ThreadIdType threadId) const`
- 이번 `RIOSend`에 실을 전송 스트림을 구성한다.
- 성공 여부와 총 송신 바이트 수를 함께 반환한다.

#### `SEND_PACKET_INFO_TO_STREAM_RETURN ReservedSendPacketInfoToStream(...) const`
- 예약/보류 성격의 송신 후보를 스트림 버퍼에 적재한다.

#### `SEND_PACKET_INFO_TO_STREAM_RETURN StoredSendPacketInfoToStream(...) const`
- 세션 send queue에서 꺼낸 송신 후보를 스트림 버퍼에 적재한다.

#### `bool RefreshRetransmissionSendPacketInfo(SendPacketInfo* sendPacketInfo, ThreadIdType threadId) const`
- 데이터 패킷의 RTO deadline을 계산하고 thread별 `RetransmissionScheduler` heap에 schedule entry를 등록한다.

---

## 3. DoRecv — 수신 등록

```cpp
[[nodiscard]]
bool DoRecv(RUDPSession& session) const override;
```

세션의 UDP 소켓에 대해 RIO 수신을 등록한다.  
완료 시 `RecvIOCompleted`가 호출된다.

```cpp
{
    std::shared_lock lock(sessionDelegate.GetSocketMutex(session));
    if (session.IsReleasing()) return true;
    if (sessionDelegate.GetSocket(session) == INVALID_SOCKET) return false;

    auto& recvBuffer = sessionDelegate.GetRecvBuffer(session);
    while (IOContext* context = recvBuffer.AcquireFreeRecvContext()) {
        context->ownerSessionGeneration = session.GetSessionGeneration();
        recvBuffer.BeginRecvIo();
        if (!rioManager.RIOReceiveEx(
                sessionDelegate.GetRecvRIORQ(session),
                context, 1,
                &context->localAddrRIOBuffer,
                &context->clientAddrRIOBuffer,
                nullptr, nullptr, 0,
                context)) {               // RequestContext는 이 slot의 IOContext
            recvBuffer.CompleteRecvIo();
            ReleaseRecvContext(context);
            return false;
        }
    }
    return true;
}
```

**호출 시점:**

| 시점 | 호출 코드 | 이유 |
|------|-----------|------|
| 세션 준비 | `InitReserveSession()` | 최초 수신 등록 |
| 수신 완료 후 | `RecvIOCompleted()` 내 `DoRecv()` | 사용 가능한 recv context 재등록 |

**`MaxOutstandingReceive = RECV_OUTSTANDING_COUNT` (현재 8):**

`DoRecv()`는 사용 가능한 recv context를 모두 `RIOReceiveEx`에 등록한다.
수신 완료 뒤에는 해제된 context를 다시 등록해 동시 수신 대기 수를 유지한다.

> 반환값을 무시하면 컴파일 경고가 발생한다. 호출 측에서 반드시 검사해야 한다.

### `ReleaseRecvContext`

```cpp
void ReleaseRecvContext(IOContext* context) const;
```

컨텍스트를 원래 `RecvBuffer`의 자유 큐(free queue)에 반환하고 수신 I/O 추적 수를 감소시킨다.
## 4. DoSend — 송신 시도 (SpinLock 방식)

```cpp
bool RUDPIOHandler::DoSend(RUDPSession& session, ThreadIdType threadId) const
```

**한 세션에 동시에 하나의 RIO Send만 허용**하는 SpinLock 패턴.

```cpp
{
    auto& sendCtx = session.rioContext.GetSendBuffer();

    // CAS: IO_NONE_SENDING → IO_SENDING
    IO_MODE expected = IO_MODE::IO_NONE_SENDING;
    if (!sendCtx.ioMode.compare_exchange_strong(
            expected, IO_MODE::IO_SENDING,
            std::memory_order_acq_rel)) {
        // 이미 다른 Send 진행 중 → 현재 Send 완료 후 자동으로 재시도됨
        return true;  // false가 아님! 에러가 아닌 정상 경쟁 상황
    }

    // IO_SENDING 상태 획득 성공 → 배치 전송 스트림 구성 + 전송
    if (!MakeSendStream(session, threadId).first) {
        // 보낼 패킷 없음 → 즉시 IO_NONE_SENDING 복원
        sendCtx.ioMode.store(IO_MODE::IO_NONE_SENDING, std::memory_order_release);
        return true;
    }

    return true;
}
```

**왜 SpinLock이 필요한가:**

```
RIO_RQ(Request Queue)는 세션당 1개.
같은 세션에 복수의 RIOSend가 동시에 등록되면:
  → 완료 순서가 보장되지 않음
  → 패킷 순서가 뒤바뀔 수 있음 (seq=5가 seq=3보다 먼저 도착)

SpinLock(CAS)으로 직렬화:
  → 한 번에 하나의 RIOSend만 진행
  → SendIOCompleted에서 IO_NONE_SENDING 복원 후 DoSend 재호출
  → 큐에 남은 패킷을 순차적으로 전송
```

---

## 5. MakeSendContext / MakeSendStream

```cpp
std::pair<bool, unsigned int> RUDPIOHandler::MakeSendStream(
    RUDPSession& session, ThreadIdType threadId) const
```

`MakeSendContext()`는 이번 송신에 사용할 `IOContext`를 준비하고,  
`MakeSendStream()`은 여러 패킷을 송신 버퍼에 묶어 단일 `RIOSend`에 실을 스트림을 구성한다.

**여러 패킷을 32KB 버퍼에 묶어 단일 `RIOSend`로 전송한다.**

```cpp
{
    auto& packetSequenceSet = sessionDelegate.GetCachedSequenceSet(session);
    packetSequenceSet.clear();

    unsigned int totalSendSize = 0;
    const size_t bufferCount = sessionDelegate.GetSendPacketInfoQueueSize(session);

    // ① reserved slot에 남은 패킷을 먼저 적재
    if (ReservedSendPacketInfoToStream(session, packetSequenceSet, totalSendSize, threadId)
        == SEND_PACKET_INFO_TO_STREAM_RETURN::OCCURED_ERROR) {
        return { false, 0 };
    }

    // ② 현재 send queue 크기만큼 front에서 pop하며 적재
    for (size_t i = 0; i < bufferCount; ++i) {
        auto result = StoredSendPacketInfoToStream(session, packetSequenceSet, totalSendSize, threadId);
        if (result == SEND_PACKET_INFO_TO_STREAM_RETURN::OCCURED_ERROR) return { false, 0 };
        if (result == SEND_PACKET_INFO_TO_STREAM_RETURN::STREAM_IS_FULL) break;
    }

    return { true, totalSendSize };
}
```

**배치 전송의 장점:**

```
패킷 3개를 개별 전송:
  RIOSend 3회 → 완료 큐 3개 항목 → DequeueCompletion 3회

패킷 3개를 배치 전송:
  memcpy × 3 → RIOSend 1회 → 완료 큐 1개 항목 → DequeueCompletion 1회

→ 커널 진입 횟수 3→1 감소
→ 완료 큐 처리 비용 감소
→ 특히 빠른 연속 전송 시 효과적
```

**32KB 한계:**

```
MAX_SEND_BUFFER_SIZE = 32768 bytes
일반 패킷 크기 ≈ 100~500 bytes
→ 한 배치에 최대 65~327개 패킷

대부분의 경우 1~10개 패킷 → 32KB 한계 도달 거의 없음
```

---

## 6. ReservedSendPacketInfoToStream / StoredSendPacketInfoToStream

- 현재 구현은 스트림 적재 책임을 두 함수로 분리한다.
- `ReservedSendPacketInfoToStream()`은 아직 즉시 송신되지 않은 후보를,
- `StoredSendPacketInfoToStream()`은 세션 송신 큐의 front를 꺼내 스트림 버퍼에 적재한다.
- `ReservedSendPacketInfoToStream()`은 reserved slot의 패킷을 먼저 적재하고 cache에 등록한다.
- `StoredSendPacketInfoToStream()`은 송신 큐 front를 적재하면서 중복 sequence 제거, erased 상태 검사, 버퍼 용량 한계 검사, 재전송 schedule 갱신을 담당한다.
- 스트림 용량을 넘는 패킷은 `SetReservedSendPacketInfo()`로 보류해 다음 send 완료 후 이어서 처리한다.

---

## 7. IOCompleted — 완료 처리 분기

```cpp
bool RUDPIOHandler::IOCompleted(
    IOContext* context,
    ULONG bytesTransferred,
    ThreadIdType threadId,
    LONG status) const
```

`IO Worker Thread`에서 `RIODequeueCompletion` 후 호출된다.

```cpp
{
    if (context == nullptr || context->session == nullptr) return false;
    RUDPSession* session = context->session;

    const bool currentGeneration =
        context->ownerSessionId == session->GetSessionId() &&
        context->ownerSessionGeneration == session->GetSessionGeneration();
    if (!currentGeneration) {
        // 현재 세션 상태는 건드리지 않고 원래 작업만 정리
        if (context->ioType == RIO_OPERATION_TYPE::OP_RECV)
            ReleaseRecvContext(context);
        else if (context->ioType == RIO_OPERATION_TYPE::OP_SEND)
            contextPool.Free(context);
        return true;
    }

    if (status != 0) {
        // recv slot·outstandingRecvIo 또는 send context·I/O mode를 먼저 정리
        if (context->ioType == RIO_OPERATION_TYPE::OP_RECV)
            ReleaseRecvContext(context);
        else {
            sessionDelegate.GetSendIOMode(*session).store(IO_MODE::IO_NONE_SENDING);
            contextPool.Free(context);
        }
        if (status == WSA_OPERATION_ABORTED && session->IsReleasing())
            return true;
        const auto reason = status == WSAECONNRESET
            ? DISCONNECT_REASON::NORMAL
            : DISCONNECT_REASON::BY_ERROR;
        session->DoDisconnect(reason);
        return true;
    }

    switch (context->ioType) {
    case RIO_OPERATION_TYPE::OP_RECV:
        if (!RecvIOCompleted(context, bytesTransferred, threadId)) {
            context->session->DoDisconnect(DISCONNECT_REASON::BY_ERROR);
            return false;
        }
        return true;

    case RIO_OPERATION_TYPE::OP_SEND:
        if (!SendIOCompleted(context, threadId)) {
            context->session->DoDisconnect(DISCONNECT_REASON::BY_ERROR);
            return false;
        }
        return true;

    default:
        return false;
    }
}
```

#### `ReleaseRecvContext` (내부 메서드)

```cpp
void ReleaseRecvContext(IOContext* context) const;
```

컨텍스트를 원래 `RecvBuffer`의 자유 큐(free queue)에 반환하고 수신 I/O 추적 수를 감소시킨다.

**`IOContext` 구조체:**

```cpp
struct IOContext {
    SessionIdType ownerSessionId;
    uint32_t ownerSessionGeneration;
    RIO_OPERATION_TYPE ioType;
    RUDPSession* session;
    RecvBuffer* ownerRecvBuffer;
    char* recvDataBuffer;                 // RECV slot의 16KB buffer
    RIO_BUF clientAddrRIOBuffer;
    RIO_BUF localAddrRIOBuffer;
    char clientAddrBuffer[sizeof(SOCKADDR_INET)];
    char localAddrBuffer[sizeof(SOCKADDR_INET)];
};
```

receive용 `IOContext`는 `RecvBufferSlot`이 `shared_ptr`로 소유한다. receive/send 요청은 RIO 등록 직전에 `ownerSessionGeneration`을 기록하며, 완료 처리는 저장된 session 포인터·ID·generation을 함께 검증한다.
## 8. RecvIOCompleted — 수신 완료

```cpp
bool RUDPIOHandler::RecvIOCompleted(
    IOContext* context,
    ULONG bytesTransferred,
    ThreadIdType threadId) const
```

```cpp
{
    auto& session = *context->session;
    auto& recvBuffer = sessionDelegate.GetRecvBuffer(session);

    // ① NetBuffer 할당 (메모리 풀, lock-free)
    NetBuffer* recvBuf = NetBuffer::Alloc();
    if (!recvBuf) {
        recvBuffer.ReleaseRecvContext(context);
        return false;
    }

    // ② 완료된 slot의 RIO buffer → NetBuffer 복사
    memcpy_s(
        recvBuf->m_pSerializeBuffer, RECV_BUFFER_SIZE,
        context->recvDataBuffer, bytesTransferred
    );
    recvBuf->m_iWrite = static_cast<WORD>(bytesTransferred);

    // ③ logic queue에 버퍼 소유권, 주소, generation을 함께 전달
    MultiSocketRUDPCoreFunctionDelegate::EnqueueContextResult(
        context, recvBuf, threadId);

    // ④ slot과 outstandingRecvIo를 정리한 뒤 사용 가능한 receive를 다시 등록
    ReleaseRecvContext(context);
    return DoRecv(session);
}
```

#### `ReleaseRecvContext`

```cpp
void ReleaseRecvContext(IOContext* context) const;
```

컨텍스트를 원래 `RecvBuffer`의 자유 큐에 반환하고 수신 I/O 추적 수를 감소시킨다.

**왜 복사가 필요한가:**

```
RIO recv slot: outstanding receive 하나에 연결된 page-locked 영역
  → 완료 context를 free queue에 반환하면 이후 DoRecv()에서 같은 slot을 재사용할 수 있음

NetBuffer: 메모리 풀에서 할당한 독립 버퍼
  → RecvLogic Worker가 자기 속도로 처리
  → 처리 완료 후 NetBuffer::Free()
```
## 9. SendIOCompleted — 송신 완료

```cpp
bool RUDPIOHandler::SendIOCompleted(
    IOContext* context,
    ThreadIdType threadId) const
```

```cpp
{
    if (context == nullptr || context->session == nullptr) return false;
    auto& session = *context->session;

    // ① IO_NONE_SENDING 복원
    sessionDelegate.GetSendIOMode(session).store(IO_MODE::IO_NONE_SENDING);

    // release 중에는 후속 send를 등록하지 않는다.
    if (session.IsReleasing()) {
        contextPool.Free(context);
        return true;
    }

    // ② 큐에 남은 패킷이 있으면 즉시 재전송 시도
    const bool result = DoSend(session, threadId);
    contextPool.Free(context);
    return result;
}
```

수신 손실 시뮬레이션, `NetBuffer` 할당 실패, 복사 실패 경로도 완료 context를 free queue에 반환한다. `RecvIOCompleted()` 또는 `SendIOCompleted()`가 `false`를 반환하면 상위 `IOCompleted()`가 세션에 `BY_ERROR` disconnect를 요청한다.

**SendIOCompleted → DoSend 재호출 패턴:**

```
[전송 흐름]

SendPacket()
  → RegisterSendPacketInfo → send queue에 추가
  → core.SendPacket() → DoSend(session, threadId)
       → CAS NONE→SENDING 성공
       → MakeSendStream (패킷 10개)
       → RefreshRetransmissionSendPacketInfo에서 데이터 패킷 schedule 등록
       → RIOSend (스트림)

RIO 완료 큐에서 SEND 완료 디큐
  → SendIOCompleted
       → IO_NONE_SENDING 복원
       → DoSend(session, threadId) 재호출
            → 새 패킷이 있으면 다시 전송
            → 없으면 즉시 IO_NONE_SENDING 복원

[병행 시나리오]
SendPacket() (다른 스레드):
  DoSend()
    → CAS NONE→SENDING 실패 (SENDING 중)
    → return true (경쟁 상황, 정상)
    → SendIOCompleted의 DoSend가 이 패킷도 함께 처리
```

---

## 10. RefreshRetransmissionSendPacketInfo

```cpp
bool RUDPIOHandler::RefreshRetransmissionSendPacketInfo(
    SendPacketInfo* sendPacketInfo,
    ThreadIdType threadId) const
```

```cpp
{
    if (sendPacketInfo->isErasedPacketInfo.load(std::memory_order_acquire)) {
        return false;
    }

    if (sendPacketInfo->isReplyType == true) {
        return true;
    }

    auto& scheduler = *retransmissionSchedulers[threadId];
    const auto now = std::chrono::steady_clock::now();
    const unsigned int rtoMs = sendPacketInfo->IsOwnerValid()
        ? sendPacketInfo->owner->GetRetransmissionTimeoutMs()
        : retransmissionMs;
    const auto deadline = now + std::chrono::milliseconds(rtoMs);

    sendPacketInfo->MarkSentForRttSample(now);
    {
        std::scoped_lock lock(scheduler.lock);
        if (sendPacketInfo->isErasedPacketInfo.load(std::memory_order_acquire)) {
            return false;
        }

        PushRetransmissionSchedule(scheduler, *sendPacketInfo, deadline);
    }

    SignalRetransmissionWakeEvent(scheduler);
    return true;
}
```

**heap schedule 방식:**

```
RetransmissionScheduler[threadId]
  ├─ priority_queue<RetransmissionHeapEntry> heap
  ├─ waitable timer
  └─ wake event

PushRetransmissionSchedule:
  → ++sendPacketInfo.scheduleVersion
  → sendPacketInfo.AddRefCount()
  → heap.push(deadline, version, info)

RunRetransmissionThread:
  → 가장 빠른 deadline만 timer로 대기
  → pop 시 erased/version mismatch는 stale entry로 폐기
```

---

### `DatagramLossSimulator`

```cpp
DatagramLossSimulator(
    unsigned int inLossPercent,
    int inSeed);
```

`inLossPercent / 100.0`을 `std::bernoulli_distribution`의 확률로 사용한다. 수신 난수 엔진은 `inSeed`, 송신 난수 엔진은 `static_cast<unsigned int>(inSeed) ^ 0x9E3779B9u`로 초기화한다.

> **전제 조건:** `inLossPercent`는 `0` 이상 `100` 이하여야 한다. 생성자와 옵션 로더는 상한을 보정하거나 거부하지 않는다.

`RUDPIOHandler`는 손실률이 `0`이면 simulator를 생성하지 않는다. 아래 두 함수는 생성된 simulator 자체의 확률 판정이며 별도 활성화 상태를 갖지 않는다.

### `ShouldDropReceivedDatagram`

```cpp
[[nodiscard]]
bool ShouldDropReceivedDatagram();
```

설정된 손실률에 따라 수신 데이터그램을 폐기할지 결정한다.

#### 반환값

| 반환값 | 조건 |
|--------|------|
| `true` | 수신 전용 난수 결과가 폐기 대상으로 결정됨 |
| `false` | 수신 전용 난수 결과가 통과 대상으로 결정됨 |

> 반환값을 무시하면 컴파일 경고가 발생한다. 호출 측에서 반드시 검사해야 한다.

> **주의:** 내부적으로 `recvLock`을 사용하여 난수 엔진 접근을 보호한다.


---

### `ShouldDropSendingDatagram`

```cpp
[[nodiscard]]
bool ShouldDropSendingDatagram();
```

설정된 `lossRate`에 따라 송신 데이터그램을 폐기할지 결정한다.

| 반환값 | 조건 |
|--------|------|
| `true` | 송신 전용 난수 결과가 폐기 대상으로 결정됨 |
| `false` | 송신 전용 난수 결과가 통과 대상으로 결정됨 |

> 반환값을 무시하면 컴파일 경고가 발생한다. 호출 측에서 반드시 검사해야 한다.

> **주의:** 내부적으로 `sendLock`을 사용하여 난수 엔진 접근을 보호한다.

## 관련 문서
- [[ThreadModel]] — IO Worker Thread에서 IOCompleted 호출
- [[PacketProcessing]] — RecvIOCompleted → RecvLogic Worker 경로
- [[RIOManager]] — RIOReceiveEx / RIOSend API 상세
- [[SendPacketInfo]] — scheduleVersion, isErasedPacketInfo, RTT 샘플링
- [[SessionComponents]] — SessionSendContext의 ioMode SpinLock
