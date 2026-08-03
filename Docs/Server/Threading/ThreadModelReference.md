# 스레드 모델 상세 레퍼런스

> 워커 구현, 시작·종료 순서, 공유 데이터 흐름을 한 번에 추적해야 할 때 사용하는 통합 레퍼런스다.
> 처음 읽는 경우 상위 [[ThreadModel|스레드 모델 허브]]에서 목적별 문서를 먼저 선택한다.

> 서버를 구성하는 모든 스레드의 역할, 동기화 방식, 생존 주기, 스레드 간 데이터 흐름을 정리한다.  
> 성능 튜닝이나 버그를 추적할 때 어떤 스레드가 어떤 작업을 하는지 파악하는 것이 핵심이다.

---

## 목차

1. [전체 구조도](#1-전체-구조도)
2. [IO Worker Thread 상세](#2-io-worker-thread-상세)
3. [RecvLogic Worker Thread 상세](#3-recvlogic-worker-thread-상세)
4. [Retransmission Thread 상세](#4-retransmission-thread-상세)
5. [Session Release Thread 상세](#5-session-release-thread-상세)
6. [Heartbeat Thread 상세](#6-heartbeat-thread-상세)
7. [스레드 시작 순서와 이유](#7-스레드-시작-순서와-이유)
8. [스레드 종료 순서와 이유](#8-스레드-종료-순서와-이유)
9. [스레드 간 공유 데이터 흐름](#9-스레드-간-공유-데이터-흐름)
10. [jthread 사용 이유](#10-jthread-사용-이유)

---

## 1. 전체 구조도

![[ThreadModel.svg]]

### 스레드 그룹 요약

| 그룹 | `THREAD_GROUP` enum | 수 | 종료 메커니즘 | 주 역할 |
|------|--------------------|----|---------------|---------|
| IO Worker | `IO_WORKER_THREAD` | N | `stop_token` | RIO 완료 큐 디큐 |
| RecvLogic Worker | `RECV_LOGIC_WORKER_THREAD` | N | `stop_token` + ManualResetEvent | 패킷 타입 분기, 핸들러 호출 |
| Retransmission | `RETRANSMISSION_THREAD` | N | `stop_token` | 미ACK 패킷 재전송 |
| Session Release | `SESSION_RELEASE_THREAD` | 1 | `stop_token` + ManualResetEvent | RELEASING 세션 정리 |
| Heartbeat | `HEARTBEAT_THREAD` | 1 | `stop_token` | 하트비트 전송, 예약 타임아웃 |
| SessionBroker | - | 1 + 4 | `stop_token` + accept 에러 | TLS 세션 발급 |
| Ticker | - | 1 | 내부 stop 신호 | TimerEvent 주기 발화 |
| Logger | - | 1 | AutoResetEvent + stop 신호 | 로그 파일 기록 |

> N = `THREAD_COUNT` 옵션 값.

---

## 2. IO Worker Thread 상세

### 역할

Windows RIO(Registered I/O)의 완료 큐를 지속적으로 폴링하여  
완료된 Send/Recv 작업을 처리한다.

### 코드 해석

```cpp
void MultiSocketRUDPCore::RunIOWorkerThread(
    const std::stop_token& stopToken, ThreadIdType threadId)
{
    TickSet tickSet;                           // 프레임 시간 추적용

    while (!stopToken.stop_requested()) {
        RIORESULT rioResults[MAX_RIO_RESULT];  // MAX_RIO_RESULT = 1024
        ULONG numOfResults = rioManager->DequeueCompletions(
            threadId, rioResults, MAX_RIO_RESULT);
        // → RIODequeueCompletion(rioCompletionQueues[threadId], results, maxResults)
        // → 블로킹 없음 (완료된 작업 없으면 0 반환)
        if (numOfResults == RIO_CORRUPT_CQ) {
            LOG_ERROR("Fatal RIO completion queue corruption; server restart is required");
            ReportFatalError({
                SERVER_FATAL_ERROR_CODE::RIO_COMPLETION_QUEUE_CORRUPT,
                threadId,
                0
            });
            return;
        }

        for (ULONG i = 0; i < numOfResults; ++i) {
            auto* context = reinterpret_cast<IOContext*>(rioResults[i].RequestContext);
            if (context == nullptr) continue;

            ioHandler->IOCompleted(
                context,
                rioResults[i].BytesTransferred,
                threadId,
                rioResults[i].Status);
        }

        // compile-time sleep mode
#if USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME == USE_WORKER_THREAD_SLEEP_FOR_FRAME
        SleepRemainingFrameTime(tickSet, workerThreadOneFrameMs);
#elif USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME == USE_WORKER_THREAD_SLEEP_ZERO
        Sleep(0); // 현재 BuildConfig.h의 선택
#endif
    }
}
```

### completion 유효성 확인과 정리

```cpp
const bool isCurrentGeneration =
    context->ownerSessionId == context->session->GetSessionId() &&
    context->ownerSessionGeneration == context->session->GetSessionGeneration();

if (!isCurrentGeneration) {
    // 새 세션 상태는 건드리지 않고 원래 request의 context만 정리한다.
}

if (status != 0) {
    // RECV는 outstandingRecvIo 감소와 slot 반환,
    // SEND는 실제 completion에서만 IO_NONE_SENDING 복구와 context 반환.
}
```

RIO 오류나 `RELEASING` 상태도 context를 먼저 버리지 않는다. 성공·오류·취소 completion 모두 `RUDPIOHandler::IOCompleted`를 통과해야 수신 카운터와 slot을 정확히 한 번 정리할 수 있다.

> **`rioResult.Status != 0` 의미:** `WSAECONNRESET`은 정상 종료 사유로 매핑하고, 해제 중 `WSA_OPERATION_ABORTED`는 예상된 취소로 처리한다. 그 외는 오류 종료로 전환한다. recv/send completion은 상태와 무관하게 slot·카운터·context를 먼저 정리한다. `StopServer()`는 모든 세션의 I/O와 logic이 drain되어 풀로 반환된 뒤에만 worker stop을 요청한다.

### 폴링 vs 이벤트

RIO는 IOCP와 달리 완료 알림 이벤트를 지원하지 않는다.  
(정확히는 IOCP 모드가 있지만 성능상 폴링이 더 효율적이다.)

```
IOCP: 완료 시 GetQueuedCompletionStatus()에서 깨어남 → 이벤트 기반, CPU 0%
RIO:  RIODequeueCompletion() 폴링 → 완료 없으면 0 반환, Sleep으로 조절

USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME=1: Sleep으로 CPU 사용률 조절
USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME=0: 완전 폴링 → 최저 레이턴시, 높은 CPU
```

---

## 3. RecvLogic Worker Thread 상세

### 역할

IO Worker가 AutoResetEvent를 signal하면 깨어나 수신 패킷의 암호화 해제,
타입 분기, 콘텐츠 핸들러 호출을 수행한다.

### 코드 해석

```cpp
void MultiSocketRUDPCore::RunRecvLogicWorkerThread(
    const std::stop_token& stopToken, ThreadIdType threadId)
{
    // 두 개의 핸들 동시 대기:
    //   eventHandles[0] = AutoResetEvent (패킷 도착 시 SetEvent)
    //   eventHandles[1] = ManualResetEvent (StopServer 시 SetEvent)
    const HANDLE eventHandles[2] = {
        recvLogicThreadEventHandles[threadId],
        recvLogicThreadEventStopHandle
    };

    while (!stopToken.stop_requested()) {
        switch (WaitForMultipleObjects(2, eventHandles, FALSE, INFINITE)) {
        case WAIT_OBJECT_0:
            // 정상: 패킷 처리
            OnRecvPacket(threadId);
            break;

        case WAIT_OBJECT_0 + 1:
            // 종료 신호: 잔여 패킷 처리 후 반환
            // LOGIC_THREAD_STOP_SLEEP_TIME = 10초
            Sleep(LOGIC_THREAD_STOP_SLEEP_TIME);
            OnRecvPacket(threadId);
            return;

        case WAIT_FAILED:
            const unsigned long errorCode = GetLastError();
            LOG_ERROR(std::format("Recv logic wait failed: {}", errorCode));
            ReportFatalError({
                SERVER_FATAL_ERROR_CODE::RECV_LOGIC_WAIT_FAILED,
                threadId,
                errorCode
            });
            return;
        }
    }
}
```

IO Worker가 완료 context를 logic queue에 넣은 뒤 `SetEvent()`에 실패한 경우에도 `RECV_LOGIC_EVENT_SIGNAL_FAILED`를 보고한다. 세 치명 오류의 callback 계약과 재시작 절차는 [[FatalErrorHandling]]에서 확인한다.

### 10초 대기의 의미

서버 종료 시퀀스:
```
1. CloseAllSessions()  ← 활성 세션을 RELEASING으로 전환
2. Release Thread가 socket close를 시작
3. IO/Logic Worker가 취소 완료와 큐 작업을 처리
4. recv I/O·logic·send drain 완료 후 세션을 풀로 반환
5. 모든 세션 반환 후 SetEvent(recvLogicThreadEventStopHandle)

현재 `Sleep(LOGIC_THREAD_STOP_SLEEP_TIME)`은 종료 이벤트 처리 시 남은 큐를 한 번 더
확인하는 방어적 유예다. 정상 종료에서는 4단계 drain이 먼저 완료되므로 이 지연에
수명 안전성을 의존하지 않는다.
```

### `OnRecvPacket` 상세

```cpp
void MultiSocketRUDPCore::OnRecvPacket(ThreadIdType threadId)
{
    RecvIOCompletedContext* completedContext = nullptr;
    while (recvIOCompletedContexts[threadId].Dequeue(&completedContext)) {

        NetBuffer* recvBuffer = completedContext->buffer;
        RUDPSession* session = completedContext->session;
        bool processingStarted = false;

        if (recvBuffer != nullptr &&
            completedContext->ownerSessionGeneration == session->GetSessionGeneration() &&
            !session->IsReleasing()) {
            completedContext->session->nowInProcessingRecvPacket.store(
                true, std::memory_order_release);
            processingStarted = true;
            packetProcessor->OnRecvPacket(
                *session,
                *recvBuffer,
                span(completedContext->clientAddrBuffer,
                     sizeof(completedContext->clientAddrBuffer))
            );
        }

        if (recvBuffer != nullptr) {
            NetBuffer::Free(recvBuffer);
        }
        if (processingStarted) {
            session->nowInProcessingRecvPacket.store(
                false, std::memory_order_release);
        }

        completedContext->ownerRecvBuffer->CompleteRecvLogic();

        // 컨텍스트 메모리 풀 반환
        recvIOCompletedContextPool.Free(completedContext);
    }
}
```

`pendingRecvLogic`은 완료 컨텍스트가 큐에 들어가기 전에 증가하고, 버퍼 처리와 폐기가
끝난 뒤 감소한다. Release Thread는 이 카운터를 `acquire`로 확인하므로 처리 시작 전후의
짧은 구간까지 drain barrier에 포함된다. `nowInProcessingRecvPacket`은 추가 방어 조건이다.

---

## 4. Retransmission Thread 상세

### 역할

스레드별 `RetransmissionScheduler`의 min-heap에서 가장 빠른 deadline을 기다렸다가 타임아웃된 패킷을 재전송한다.
재전송 횟수가 한계를 초과하면 세션을 `BY_RETRANSMISSION`으로 강제 종료한다.

### 코드 해석

```cpp
void MultiSocketRUDPCore::RunRetransmissionThread(
    const std::stop_token& stopToken, ThreadIdType threadId)
{
    auto& scheduler = *retransmissionSchedulers[threadId];
    const HANDLE waitHandles[3] = {
        scheduler.timerHandle,
        scheduler.wakeEventHandle,
        retransmissionStopEventHandle
    };

    std::vector<SendPacketInfo*> dueList;
    while (!stopToken.stop_requested()) {
        dueList.clear();
        bool hasNext = false;
        std::chrono::steady_clock::time_point nextDeadline{};

        {
            std::scoped_lock lock(scheduler.lock);
            const auto now = std::chrono::steady_clock::now();

            while (!scheduler.heap.empty()) {
                const auto& top = scheduler.heap.top();
                if (top.info->isErasedPacketInfo.load(std::memory_order_acquire) ||
                    top.version != top.info->scheduleVersion) {
                    SendPacketInfo::Free(top.info);
                    scheduler.heap.pop();
                    continue;
                }

                if (top.deadline > now) {
                    hasNext = true;
                    nextDeadline = top.deadline;
                    break;
                }

                top.info->InvalidateRttSample();
                dueList.push_back(top.info);
                scheduler.heap.pop();
            }
        }

        for (auto* info : dueList) {
            ProcessRetransmission(info, threadId);
        }

        if (!dueList.empty()) {
            continue;
        }

        if (hasNext) {
            ArmRetransmissionTimer(scheduler.timerHandle, nextDeadline);
            WaitForMultipleObjects(3, waitHandles, FALSE, INFINITE);
        }
        else {
            WaitForMultipleObjects(2, emptyWaitHandles, FALSE, INFINITE);
        }
    }
}
```

### `AddRefCount` / `Free` 설계

```cpp
struct SendPacketInfo {
    std::atomic_int32_t refCount = 0;
    uint64_t scheduleVersion = 0;

    void AddRefCount() {
        refCount.fetch_add(1, std::memory_order_relaxed);
    }

    static void Free(SendPacketInfo* info) {
        if (info->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            NetBuffer::Free(info->buffer);
            sendPacketInfoPool->Free(info);
        }
    }
};
```

`PushRetransmissionSchedule()`은 heap entry를 추가할 때 `scheduleVersion`을 증가시키고 `AddRefCount()`를 호출한다.
ACK 처리나 재송신으로 인해 오래된 heap entry가 남아도, pop 시 `isErasedPacketInfo` 또는 version mismatch를 확인해 stale entry로 폐기한다.

**참조 카운팅이 필요한 이유:**  
재전송 scheduler heap entry가 `SendPacketInfo`를 들고 있는 동안,
Logic Worker가 ACK를 받아 `sendPacketInfoMap`에서 같은 객체를 제거할 수 있다.
RefCount가 1보다 크면 메모리를 실제로 해제하지 않아 heap entry 처리 중 use-after-free를 방지한다.

---

## 5. Session Release Thread 상세

### 역할

`DoDisconnect(reason)`에 의해 RELEASING 상태가 된 세션의 소켓을 닫고 풀에 반환한다.  
IO 완료 여부를 확인해 race condition을 방지한다.

### 코드 해석

```cpp
void MultiSocketRUDPCore::RunSessionReleaseThread(const std::stop_token& stopToken)
{
    const HANDLE eventHandles[2] = {
        sessionReleaseEventHandle,     // AutoResetEvent (PushToDisconnectTargetSession에서 Set)
        sessionReleaseStopEventHandle  // ManualResetEvent (StopServer에서 Set)
    };

    while (!stopToken.stop_requested()) {
        WaitForMultipleObjects(2, eventHandles, FALSE, INFINITE);

        // 현재 해제 목록 스냅샷
        std::vector<SessionIdType> copyList;
        {
            std::scoped_lock lock(releaseSessionIdListLock);
            copyList = releaseSessionIdList;
            releaseSessionIdList.clear();
        }

        std::vector<SessionIdType> remainList;  // 아직 바쁜 세션들

        for (auto id : copyList) {
            auto* session = sessionManager->GetReleasingSession(id);
            if (session == nullptr) continue;

            session->BeginIOShutdown(); // recv logic quiescence 뒤 OnDisconnected + socket close-only
            if (!session->CanFinalizeIO()) {
                // 10초 경과 시 카운터와 send mode를 로그로 남기되 강제로 변경하지 않는다.
                remainList.push_back(id);
                continue;
            }

            // 안전 확인 완료 → 실제 해제
            session->Disconnect();
            // → FinalizeRIOCleanup(), OnReleased()
            // → DisconnectSession(id) → ReleaseSession에서 InitializeSession()/SetDisconnected()
        }

        // 아직 바쁜 세션 → 재시도
        if (!remainList.empty()) {
            std::scoped_lock lock(releaseSessionIdListLock);
            releaseSessionIdList.insert(
                releaseSessionIdList.end(),
                remainList.begin(), remainList.end());
            SetEvent(sessionReleaseEventHandle);  // 자기 자신 깨우기
        }
    }
}
```

이 worker는 사용자 recv logic이 끝난 뒤 `OnDisconnected()`를 한 번 호출하고 소켓만 먼저 닫는다. 이후 send I/O, `outstandingRecvIo`, `activeIOCompletions`가 끝날 때까지 세션을 `RELEASING`으로 유지한다. close와 경합해 들어온 stale logic도 `pendingRecvLogic`으로 추적한다. 10초 제한은 강제 반환이 아니라 지연 진단 기준이다.

### AutoResetEvent vs ManualResetEvent

| 이벤트 | 타입 | 사용 |
|--------|------|------|
| `sessionReleaseEventHandle` | AutoReset | `PushToDisconnectTargetSession`이 SetEvent → 한 번 처리 후 자동 Reset |
| `sessionReleaseStopEventHandle` | ManualReset | `StopServer`가 SetEvent → 이후 계속 Signaled 상태 유지 → 종료 |

`remainList`가 있을 때 다시 `SetEvent`를 호출하는 이유:  
AutoReset 이벤트는 하나의 Wait에만 Signal을 전달한다.  
재시도를 위해 다시 자신을 깨워야 한다. 이것은 SpinWait 없이 이벤트 기반으로 재시도하는 패턴이다.

---

## 6. Heartbeat Thread 상세

### 역할

주기적으로 연결된 세션에 하트비트를 전송하고,  
RESERVED 상태 세션의 타임아웃을 감지한다.

### 코드 해석

```cpp
void MultiSocketRUDPCore::RunHeartbeatThread(const std::stop_token& stopToken) const
{
    TickSet tickSet;

    while (!stopToken.stop_requested()) {
        uint64_t now = GetTickCount64();

        for (auto* session : sessionManager->GetSessionList()) {
            if (!session->IsUsingSession()) continue;

            if (session->IsConnected()) {
                // ① CONNECTED 세션: 하트비트 전송
                sessionDelegate.SendHeartbeatPacket(*session);
                // → HEARTBEAT_TYPE 패킷 전송 (AES-GCM, 재전송 추적됨)

            } else if (session->IsReserved()) {
                // ② RESERVED 세션: 30초 타임아웃 체크
                if (sessionDelegate.CheckReservedSessionTimeout(*session, now)) {
                    // RUDPSession::reservedSessionTimeoutMs 기본값 = 30000ms
                    sessionDelegate.AbortReservedSession(*session);
                    // → TryAbortReserved() CAS: RESERVED → RELEASING
                    // → 공통 release queue 등록
                    // → close → drain → cleanup → ReleaseSession
                }
            }
        }

        SleepRemainingFrameTime(tickSet, heartbeatThreadSleepMs);
    }
}
```

### 하트비트와 세션 생존 감지의 관계

```
[서버]                               [클라이언트]
SendHeartbeatPacket()
  → HEARTBEAT_TYPE + sequence ──────────────────►
                                     수신 후 즉시
           ◄── HEARTBEAT_REPLY_TYPE + same_sequence

[서버] OnSendReply(sequence)
  → sendPacketInfoMap에서 제거
  → flowManager.OnAckReceived()
  → 재전송 카운트 리셋

재전송 한계 초과(클라이언트 응답 없음):
  RunRetransmissionThread → ++retransmissionCount >= max
  → session->DoDisconnect(DISCONNECT_REASON::BY_RETRANSMISSION)  ← 서버 측에서 종료 감지
```

---

## 7. 스레드 시작 순서와 이유

```
1. Ticker::Start(timerTickMs)
   └─ 이유: 타이머 이벤트가 스레드 시작 전에 활성화될 수 있어야 함

2. 이벤트 핸들 생성 (for N스레드)
   └─ recvLogicThreadEventHandles[i] = CreateEvent(NULL, FALSE, FALSE, NULL)
   └─ 이유: RecvLogic Worker가 시작 전에 핸들이 준비되어야 함

3. SESSION_RELEASE_THREAD 시작
   └─ 이유: IO Worker가 시작하기 전에 세션 해제 준비 완료

4. HEARTBEAT_THREAD 시작
   └─ 이유: 조기 연결된 세션이 없으므로 순서 무관, 조기 시작이 안전

5. IO_WORKER_THREAD × N 시작
   └─ 이유: 실제 I/O 처리의 핵심; RIO 큐 디큐 시작

6. RECV_LOGIC_WORKER_THREAD × N 시작
   └─ 이유: IO Worker의 AutoResetEvent signal에 응답 준비

7. RETRANSMISSION_THREAD × N 시작
   └─ 이유: 이 시점에 retransmissionSchedulers와 wake/timer handle이 초기화됨

8. Sleep(1000)
   └─ 이유: 모든 스레드가 완전히 실행 상태가 될 때까지 대기

9. RUDPSessionBroker::Start()
   └─ 이유: 모든 인프라가 준비된 후 클라이언트 수락 시작
```

---

## 8. 스레드 종료 순서와 이유

```
1. SessionBroker::Stop()
   └─ 이유: 새 클라이언트 차단 (이후 진행 중 세션들만 정리)

2. CloseAllSessions()
   └─ 이유: 활성 세션을 RELEASING으로 전환

3. 세션 socket close-only 후 모든 send/receive/logic 작업 drain
   └─ 이유: RIO buffer와 context를 worker보다 먼저 파괴하지 않기 위해

4. 모든 세션의 RIO cleanup과 pool 반환 완료

5. SetEvent(recvLogicThreadEventStopHandle)
   └─ 이유: Logic Worker들에게 종료 신호 (10초 후 잔여 패킷 처리 후 종료)

6. SetEvent(sessionReleaseStopEventHandle)
   └─ 이유: Release Thread에게 종료 신호

   SetEvent(retransmissionStopEventHandle)
   └─ 이유: Retransmission Thread의 event wait 해제

7. StopAllThreads()
   └─ RUDPThreadManager::RequestStop(각 그룹)
   └─ stop_token 신호 → jthread 자동 join

종료 순서가 중요한 이유:
  - IO Worker보다 먼저 Logic Worker를 종료하면: 완료 컨텍스트 큐가 남지만 처리되지 않음
  - 세션 해제 전 스레드 종료: DoDisconnect 후 Disconnect가 호출 안 됨 → 세션 풀 고갈
  - Logger 이전에 다른 스레드 종료: 종료 중 발생한 로그 유실 가능
```

worker stop은 모든 세션이 unused pool로 반환된 뒤에만 요청한다. drain이 지연되면 로그를 남기며, 카운터나 send mode를 강제로 완료 상태로 바꾸지 않는다.

---

## 9. 스레드 간 공유 데이터 흐름

```
[IO Worker thread=0]
  │
  ├─ RIODequeueCompletion → OP_RECV 완료 감지
  │   → NetBuffer::Alloc() + memcpy(recvBuffer → newBuffer)
  │   → recvIOCompletedContextPool.Alloc()
  │   → context가 newBuffer + session generation 직접 소유
  │   → recvIOCompletedContexts[0].Enqueue(context)
  │   → SetEvent(recvLogicEventHandles[0])               ← 깨움
  │   → DoRecv(session)                                  ← 다음 수신 즉시 등록
  │
  └─ RIODequeueCompletion → OP_SEND 완료 감지
      → InterlockedExchange(ioMode, IO_NONE_SENDING)
      → DoSend(session, threadId)                        ← 큐에 남은 패킷 전송

[RecvLogic Worker thread=0]
  │
  ├─ WaitForMultipleObjects 대기
  │   → semaphore 신호 → 깨어남
  │
  ├─ recvIOCompletedContexts[0].Dequeue(context)
  │   → session.nowInProcessingRecvPacket = true
  │   → context generation 재검증 + context.buffer 사용
  │   → packetProcessor.OnRecvPacket(session, buffer, clientAddr)
  │       → [타입 분기] → session.OnRecvPacket(buffer)
  │           → SessionPacketOrderer.OnReceive()
  │               → ProcessPacket()
  │                   → packetFactoryMap[id](session, buffer)()
  │                       → (콘텐츠 핸들러 호출)
  │               → SendReplyToClient(sequence)         ← ACK 전송
  │   → session.nowInProcessingRecvPacket = false
  │
  └─ OnSendReply 수신 시:
      → sendPacketInfoMap.FindAndErase(sequence)
      → core.MarkSendPacketInfoErased(info, threadId)   ← heap entry는 pop 시 stale 처리
      → SendPacketInfo::Free(info)
      → TryFlushPendingQueue()

[Retransmission Thread thread=0]
  │
  └─ RetransmissionScheduler[0].heap 대기
      → waitable timer로 가장 빠른 deadline까지 대기
      → isErasedPacketInfo 또는 scheduleVersion mismatch 확인
      → stale entry면 Free 후 skip
      → core.SendPacket(info)                           ← 재전송
      → retransmissionCount >= max → session.DoDisconnect(DISCONNECT_REASON::BY_RETRANSMISSION)

[Session Release Thread]
  │
  ├─ WaitForMultipleObjects
  │   → sessionReleaseEventHandle 신호 (PushToDisconnectTargetSession에서 Set)
  │
  ├─ GetReleasingSession(id)
  │   → recv logic quiescence 확인
  │   → BeginIOShutdown(): OnDisconnected + socket close-only
  │   → send I/O / outstandingRecvIo / pendingRecvLogic / activeIOCompletions 체크
  │   → nowInProcessingRecvPacket 추가 확인
  │
  └─ 안전 확인 후 session.Disconnect()
      → FinalizeRIOCleanup()
      → ForEachAndClearSendPacketInfoMap
          → core.MarkSendPacketInfoErased(info, threadId)
          → SendPacketInfo::Free(info)
      → OnReleased()
      → sessionManager.ReleaseSession(id)
          → InitializeSession()
          → SetDisconnected()
          → unusedSessionIdList.push_back(id)

[Heartbeat Thread]
  │
  └─ session.SendHeartbeatPacket()
      → HEARTBEAT_TYPE 패킷 전송
      → retransmissionSchedulers[session.threadId]에 schedule 등록
      → Retransmission Thread가 추적
```

---

## 10. jthread 사용 이유

```cpp
// C++20 std::jthread 특징
std::jthread t([](std::stop_token st) {
    while (!st.stop_requested()) {
        // 작업...
    }
});
// t 소멸 시 request_stop() + join() 자동 호출
```

**thread 대비 jthread의 이점:**

| 기능 | `thread` | `jthread` |
|------|----------|-----------|
| 소멸자에서 자동 join | ❌ (미join 시 terminate) | ✅ |
| 협력적 종료 (`stop_token`) | ❌ | ✅ |
| 종료 요청 (`request_stop()`) | ❌ | ✅ |

**RUDPThreadManager:**

```cpp
class RUDPThreadManager {
    using ThreadGroup = std::vector<std::jthread>;
    std::unordered_map<THREAD_GROUP, ThreadGroup> threadGroups;

public:
    template<typename Func, typename... Args>
    void Start(THREAD_GROUP group, int count, Func&& func, Args&&... args) {
        auto& group_vec = threadGroups[group];
        for (int i = 0; i < count; ++i) {
            group_vec.emplace_back(func, args..., i);
            // jthread 생성 = 스레드 시작
        }
    }

    void Stop(THREAD_GROUP group) {
        auto& group_vec = threadGroups[group];
        for (auto& t : group_vec) {
            t.request_stop();   // stop_token 신호
        }
        // jthread 소멸 시 자동 join
        group_vec.clear();
    }
};
```

---

## 관련 문서
- [[MultiSocketRUDPCore]] — 스레드 시작/종료 코드
- [[RUDPIOHandler]] — IO Worker가 호출하는 RecvIOCompleted / SendIOCompleted
- [[PacketProcessing]] — RecvLogic Worker의 OnRecvPacket 처리
- [[SendPacketInfo]] — 재전송 스레드의 추적 구조체
- [[SessionLifecycle]] — Session Release Thread의 Disconnect 흐름
