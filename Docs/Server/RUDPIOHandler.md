# RUDPIOHandler

> **RIO 기반 비동기 I/O의 실행과 완료 처리를 담당.**  
> `DoRecv`, `DoSend`, `IOCompleted` 세 핵심 메서드로 수신·송신·완료를 관리하며,  
> 복수의 `SendPacketInfo`를 하나의 RIO Send 스트림으로 묶는 배치 전송을 수행한다.

---

## 목차

1. [위치와 의존 관계](#1-위치와-의존-관계)
2. [함수 설명](#2-함수-설명)
3. [DoRecv — 수신 등록](#3-dorecv--수신-등록)
4. [DoSend — 송신 시도 (SpinLock 방식)](#4-dosend--송신-시도-spinlock-방식)
5. [MakeSendContext / MakeSendStream](#5-makesendcontext--makesendstream)
6. [ReservedSendPacketInfoToStream / StoredSendPacketInfoToStream](#6-reservedsendpacketinfotostream--storedsendpacketinfotostream)
7. [IOCompleted — 완료 처리 분기](#7-iocompleted--완료-처리-분기)
8. [RecvIOCompleted — 수신 완료](#8-recviocompletd--수신-완료)
9. [SendIOCompleted — 송신 완료](#9-sendiocompletd--송신-완료)
10. [RefreshRetransmissionSendPacketInfo](#10-refreshretransmissionsendpacketinfo)

---

## 1. 위치와 의존 관계

```
RUDPIOHandler
 ├── IRIOManager&           ← RIOReceiveEx / RIOSend 호출
 ├── ISessionDelegate&      ← 세션 내부 접근 (shared_ptr 없이 인터페이스)
 ├── CTLSMemoryPool<IOContext>&  ← Send IOContext TLS 풀 (lock-free)
 ├── vector<list<SendPacketInfo*>>& sendPacketInfoList[N]   ← 스레드별 재전송 목록
 └── vector<unique_ptr<mutex>>&  sendPacketInfoListLock[N]  ← 목록 보호 뮤텍스
```

---

## 2. 함수 설명

### 공개 함수

#### `RUDPIOHandler(IRIOManager& inRioManager, ISessionDelegate& inSessionDelegate, CTLSMemoryPool<IOContext>& contextPool, std::vector<std::list<SendPacketInfo*>>& sendPacketInfoList, std::vector<std::unique_ptr<std::mutex>>& sendPacketInfoListLock, BYTE inMaxHoldingPacketQueueSize, unsigned int inRetransmissionMs)`
- RIO 매니저, 세션 delegate, IOContext 풀, 스레드별 전송 추적 목록과 락을 주입받는다.
- 보류 큐 크기와 재전송 주기 같은 정책값도 생성 시점에 고정된다.

#### `bool IOCompleted(IOContext* context, ULONG transferred, BYTE threadId) const`
- IO Worker에서 호출되는 완료 분기 진입점이다.
- `context->ioType`에 따라 RECV와 SEND 완료 처리를 분기한다.

#### `bool DoRecv(const RUDPSession& session) const`
- 세션에 다음 RIO Receive를 등록한다.

#### `bool DoSend(RUDPSession& session, ThreadIdType threadId) const`
- 세션 송신 시도 진입점이다.
- 세션 단위 `ioMode` CAS로 동시 송신을 직렬화한다.

### 비공개 함수

#### `bool RecvIOCompleted(IOContext* contextResult, ULONG transferred, BYTE threadId) const`
- 수신 완료 데이터를 `NetBuffer`로 복사해 RecvLogic Worker로 넘기고, 곧바로 다음 `DoRecv()`를 재등록한다.

#### `bool SendIOCompleted(IOContext* context, BYTE threadId) const`
- 송신 완료 후 `IO_NONE_SENDING`을 복구하고 후속 송신을 이어간다.

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
- 재전송 추적 목록에 저장된 송신 후보를 스트림 버퍼에 적재한다.

#### `bool RefreshRetransmissionSendPacketInfo(SendPacketInfo* sendPacketInfo, ThreadIdType threadId) const`
- 재전송 타임스탬프와 스레드별 추적 목록 상태를 갱신한다.

---

## 3. DoRecv — 수신 등록

```cpp
bool RUDPIOHandler::DoRecv(const RUDPSession& session) const
```

세션의 UDP 소켓에 대해 RIO 수신을 등록한다.  
완료 시 `RecvIOCompleted`가 호출된다.

```cpp
{
    auto& recvCtx = session.rioContext.GetRecvBuffer();

    // 소켓 락 획득 (shared: CloseSocket의 unique_lock과 경쟁)
    std::shared_lock lock(session.GetSocketContext().GetSocketMutex());
    if (session.GetSocketContext().GetSocket() == INVALID_SOCKET) return false;

    return rioManager.RIOReceiveEx(
        recvCtx.GetRIORQ(),              // 세션 RIO 요청 큐
        recvCtx.GetRecvRIOBuffer(),      // 수신 데이터 버퍼 (16KB)
        1,                               // 버퍼 슬라이스 수
        recvCtx.GetLocalAddrRIOBuffer(), // 로컬 주소 버퍼 (미사용, 등록 필요)
        recvCtx.GetClientAddrRIOBuffer(),// 클라이언트 주소 버퍼 (→ CanProcessPacket)
        nullptr, nullptr,                // 제어 메시지 (미사용)
        0,                               // 플래그
        reinterpret_cast<void*>(
            reinterpret_cast<uintptr_t>(&session))
        // RequestContext: IOCompleted에서 세션 포인터 복원
    );
}
```

**호출 시점:**

| 시점 | 호출 코드 | 이유 |
|------|-----------|------|
| 세션 준비 | `InitReserveSession()` | 최초 수신 등록 |
| 수신 완료 후 | `RecvIOCompleted()` 내 `DoRecv()` | 연속 수신 (1→완료→재등록) |

**`MaxOutstandingReceive = 1` 이유:**

UDP 데이터그램은 하나의 `RIOReceiveEx`가 완전한 패킷 1개를 수신한다.  
수신 완료 즉시 다음 `DoRecv`를 호출해 연속 수신을 유지하므로  
한 번에 1개만 등록해도 충분하다.

---

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
    auto& sendCtx = session.rioContext.GetSendBuffer();
    char* streamBuffer = sendCtx.GetRIOSendBuffer();  // 32KB
    int offset = 0;

    // ① 재전송 목록에서 미전송/타임아웃 패킷 수집
    std::vector<SendPacketInfo*> pendingInfos;
    {
        std::shared_lock lock(*sendPacketInfoListLock[threadId]);
        for (auto* info : sendPacketInfoList[threadId]) {
            if (info->isErasedPacketInfo.load(std::memory_order_acquire)) continue;
            if (info->owner != &session) continue;

            // 중복 방지 (cachedSequenceSet)
            if (!sendCtx.cachedSequenceSet.insert(info->sendPacketSequence).second)
                continue;

            pendingInfos.push_back(info);
        }
    }
    sendCtx.cachedSequenceSet.clear();

    if (pendingInfos.empty()) return false;

    // ② 패킷들을 스트림 버퍼에 순서대로 복사
    for (auto* info : pendingInfos) {
        int packetSize = info->buffer->m_iWriteLast;

        if (offset + packetSize > MAX_SEND_BUFFER_SIZE_BYTES) break;  // 32KB 초과
        memcpy(streamBuffer + offset,
               info->buffer->m_pSerializeBuffer,
               packetSize);

        // ③ 재전송 타임스탬프 갱신
        RefreshRetransmissionSendPacketInfo(info, threadId);

        offset += packetSize;
    }

    if (offset == 0) return false;

    // ④ 송신 컨텍스트를 준비한 뒤 TryRIOSend로 실제 RIO 송신 시도
    auto [ok, context] = MakeSendContext(session, threadId);
    if (!ok) return false;
    if (!TryRIOSend(session, context)) return false;

    return true;
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
MAX_SEND_BUFFER_SIZE_BYTES = 32768 bytes
일반 패킷 크기 ≈ 100~500 bytes
→ 한 배치에 최대 65~327개 패킷

대부분의 경우 1~10개 패킷 → 32KB 한계 도달 거의 없음
```

---

## 6. ReservedSendPacketInfoToStream / StoredSendPacketInfoToStream

- 현재 구현은 스트림 적재 책임을 두 함수로 분리한다.
- `ReservedSendPacketInfoToStream()`은 아직 즉시 송신되지 않은 후보를,
- `StoredSendPacketInfoToStream()`은 재전송 추적 목록에 있는 후보를 수집한다.
- 두 함수 모두 중복 sequence 제거, erased 상태 검사, 버퍼 용량 한계 검사를 담당한다.

---

## 7. IOCompleted — 완료 처리 분기

```cpp
void RUDPIOHandler::IOCompleted(
    IOContext* context,
    ULONG bytesTransferred,
    ThreadIdType threadId)
```

`IO Worker Thread`에서 `RIODequeueCompletion` 후 호출된다.

```cpp
{
    switch (context->ioType) {
    case IO_TYPE::RECV:
        RecvIOCompleted(context, bytesTransferred, threadId);
        break;

    case IO_TYPE::SEND:
        SendIOCompleted(context, threadId);
        break;

    default:
        LOG_ERROR(std::format("Unknown IO type: {}", static_cast<int>(context->ioType)));
        break;
    }
}
```

**`IOContext` 구조체:**

```cpp
struct IOContext {
    IO_TYPE   ioType;         // RECV 또는 SEND
    RUDPSession* session;     // 소유 세션 (GetIOCompletedContext에서 설정)
    SessionIdType ownerSessionId;  // 세션 ID (소유권 확인용)

    // RECV 전용: 클라이언트 주소 복사본
    char clientAddrBuffer[sizeof(SOCKADDR_INET)];
};
```

---

## 8. RecvIOCompleted — 수신 완료

```cpp
bool RUDPIOHandler::RecvIOCompleted(
    IOContext* context,
    ULONG bytesTransferred,
    ThreadIdType threadId)
```

```cpp
{
    auto& session = *context->session;

    // ① NetBuffer 할당 (메모리 풀, lock-free)
    NetBuffer* recvBuf = NetBuffer::Alloc();
    if (!recvBuf) {
        LOG_ERROR("NetBuffer::Alloc failed");
        session.DoDisconnect();
        return false;
    }

    // ② RIO recv 버퍼 → NetBuffer 복사 (page-locked → 일반 힙)
    auto& recvCtx = session.rioContext.GetRecvBuffer();
    memcpy_s(
        recvBuf->m_pSerializeBuffer, RECV_BUFFER_SIZE,
        recvCtx.GetRecvBuffer(),     bytesTransferred
    );
    recvBuf->m_iWrite = static_cast<WORD>(bytesTransferred);

    // ③ 수신 버퍼 큐에 삽입 (RecvLogic Worker가 꺼냄)
    session.EnqueueToRecvBufferList(recvBuf);

    // ④ RecvLogic Worker에 알림
    auto* completedCtx = recvIOCompletedContextPool.Alloc();
    completedCtx->Initialize(context);  // session + clientAddrBuffer 복사
    MultiSocketRUDPCoreFunctionDelegate::EnqueueContextResult(completedCtx, threadId);
    // → recvIOCompletedContexts[threadId].Enqueue(completedCtx)
    // → ReleaseSemaphore(recvLogicEventHandles[threadId], 1, nullptr)

    // ⑤ 즉시 다음 수신 등록 (연속 수신)
    return DoRecv(session);
}
```

**왜 복사가 필요한가:**

```
RIO recv 버퍼: 세션 소켓에 page-locked된 고정 영역
  → DoRecv() 즉시 재호출 → 다음 패킷이 같은 버퍼에 덮어씌워짐

NetBuffer: 메모리 풀에서 할당한 독립 버퍼
  → RecvLogic Worker가 자기 속도로 처리
  → 처리 완료 후 NetBuffer::Free()
```

---

## 9. SendIOCompleted — 송신 완료

```cpp
void RUDPIOHandler::SendIOCompleted(
    IOContext* context,
    ThreadIdType threadId)
```

```cpp
{
    auto& session = *context->session;
    auto& sendCtx = session.rioContext.GetSendBuffer();

    // ① IO_NONE_SENDING 복원
    sendCtx.ioMode.store(IO_MODE::IO_NONE_SENDING, std::memory_order_release);

    // ② 큐에 남은 패킷이 있으면 즉시 재전송 시도
    DoSend(session, threadId);
    // → CAS IO_NONE_SENDING → IO_SENDING
    // → MakeSendStream → RIOSend (패킷이 있으면)
    // → 없으면 IO_NONE_SENDING 즉시 복원

    // ③ IOContext 반환 (TLS 풀)
    sendIOContextPool.Free(context);
}
```

**SendIOCompleted → DoSend 재호출 패턴:**

```
[전송 흐름]

SendPacket()
  → RegisterSendPacketInfo → sendPacketInfoList에 추가
  → core.SendPacket() → DoSend(session, threadId)
       → CAS NONE→SENDING 성공
       → MakeSendStream (패킷 10개)
       → RIOSend (스트림)

RIO 완료 큐에서 SEND 완료 디큐
  → SendIOCompleted
       → NONE_SENDING 복원
       → DoSend(session, threadId) 재호출
            → 새 패킷이 있으면 다시 전송
            → 없으면 즉시 NONE_SENDING 복원

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
void RUDPIOHandler::RefreshRetransmissionSendPacketInfo(
    SendPacketInfo& info,
    ThreadIdType threadId,
    unsigned long long newTimestamp)
```

```cpp
{
    // ① 타임스탬프 갱신
    info.retransmissionTimeStamp = newTimestamp;

    // ② 재전송 목록에서 이터레이터 위치 갱신 (O(1) 삭제 위해)
    if (!info.isInSendPacketInfoList) {
        // 처음 등록: list의 끝에 추가하고 이터레이터 저장
        std::scoped_lock lock(*sendPacketInfoListLock[threadId]);
        info.listItor = sendPacketInfoList[threadId].insert(
            sendPacketInfoList[threadId].end(), &info);
        info.isInSendPacketInfoList = true;
    }
    // 이미 등록된 경우: listItor가 여전히 유효 (list는 삽입/삭제 시 다른 이터레이터 무효화 없음)
}
```

**이터레이터 저장으로 O(1) 삭제:**

```
sendPacketInfoList[threadId]: std::list<SendPacketInfo*>
  → 임의 위치 erase는 이터레이터가 있으면 O(1)
  → 이터레이터 없이 찾으려면 O(N) 순회

info.listItor 저장:
  → EraseSendPacketInfo에서 sendPacketInfoList[threadId].erase(info.listItor)
  → O(1) 삭제

std::vector가 아닌 std::list 이유:
  → erase(iterator) → O(1) (이웃 포인터만 변경)
  → vector.erase(pos) → O(N) (뒤 요소 이동)
  → 재전송 스레드가 자주 이 목록을 순회하고 삭제하므로 list가 적합
```

---

## 관련 문서
- [[ThreadModel]] — IO Worker Thread에서 IOCompleted 호출
- [[PacketProcessing]] — RecvIOCompleted → RecvLogic Worker 경로
- [[RIOManager]] — RIOReceiveEx / RIOSend API 상세
- [[SendPacketInfo]] — listItor O(1) 삭제, isErasedPacketInfo
- [[SessionComponents]] — SessionSendContext의 ioMode SpinLock
