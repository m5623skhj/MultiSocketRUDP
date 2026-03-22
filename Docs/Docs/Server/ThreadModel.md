# 스레드 모델

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

        for (ULONG i = 0; i < numOfResults; ++i) {
            auto* context = GetIOCompletedContext(rioResults[i]);
            if (context == nullptr) continue;   // 세션 이미 해제됨

            ioHandler->IOCompleted(
                context,
                rioResults[i].BytesTransferred,
                threadId);
        }

        // 선택적 프레임 슬립
#if USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME
        SleepRemainingFrameTime(tickSet, workerThreadOneFrameMs);
#endif
    }
}
```

### `GetIOCompletedContext` 상세

```cpp
IOContext* MultiSocketRUDPCore::GetIOCompletedContext(const RIORESULT& rioResult) const
{
    auto* context = reinterpret_cast<IOContext*>(rioResult.RequestContext);
    if (context == nullptr) return nullptr;

    // 세션이 아직 사용 중인지 확인
    context->session = sessionManager->GetUsingSession(context->ownerSessionId);
    // → sessionList[ownerSessionId]->IsUsingSession() ? 반환 : nullptr

    if (context->session == nullptr) return nullptr;  // 해제된 세션

    // RIO 에러 처리
    if (rioResult.Status != 0) {
        context->session->DoDisconnect();
        return nullptr;
    }

    return context;
}
```

> **`rioResult.Status != 0` 의미:** 소켓 오류, 원격 호스트 닫힘 등.  
> `StopServer()`에서 `CloseAllSessions()`가 소켓을 닫으면 진행 중인 RIO 작업에 에러가 발생해  
> `Status != 0`으로 완료 큐에 들어온다. 이것이 IO Worker Thread를 정상 종료시키는 메커니즘.

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

IO Worker가 Semaphore를 Release하면 깨어나 수신 패킷의 암호화 해제,  
타입 분기, 콘텐츠 핸들러 호출을 수행한다.

### 코드 해석

```cpp
void MultiSocketRUDPCore::RunRecvLogicWorkerThread(
    const std::stop_token& stopToken, ThreadIdType threadId)
{
    // 두 개의 핸들 동시 대기:
    //   eventHandles[0] = Semaphore (패킷 도착 시 Release(1))
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
        }
    }
}
```

### 10초 대기의 의미

서버 종료 시퀀스:
```
1. CloseAllSessions()  ← 소켓 닫힘 → RIO 에러 완료 → IO Worker가 세션 DoDisconnect
2. SetEvent(recvLogicThreadEventStopHandle)  ← Logic Worker 종료 신호

그러나 CloseAllSessions 이후에도 Semaphore에 이미 올라간 패킷이 존재.
→ 10초 대기 후 OnRecvPacket()으로 잔여 패킷 처리
→ 패킷 유실 없이 그레이스풀 셧다운
```

### `OnRecvPacket` 상세

```cpp
void MultiSocketRUDPCore::OnRecvPacket(ThreadIdType threadId)
{
    RecvIOCompletedContext* completedContext = nullptr;
    while (recvIOCompletedContexts[threadId].Dequeue(&completedContext)) {

        // 패킷 처리 중 플래그 설정 (Session Release Thread 대기용)
        completedContext->session->nowInProcessingRecvPacket.store(
            true, std::memory_order_seq_cst);

        NetBuffer* recvBuffer = nullptr;
        completedContext->session->rioContext.GetRecvBuffer()
            .recvBufferList.Dequeue(&recvBuffer);

        if (recvBuffer != nullptr) {
            packetProcessor->OnRecvPacket(
                *completedContext->session,
                *recvBuffer,
                span(completedContext->clientAddrBuffer,
                     sizeof(completedContext->clientAddrBuffer))
            );
        }

        // 처리 완료 플래그 해제
        completedContext->session->nowInProcessingRecvPacket.store(
            false, std::memory_order_seq_cst);

        // 컨텍스트 메모리 풀 반환
        recvIOCompletedContextPool.Free(completedContext);
    }
}
```

**`nowInProcessingRecvPacket` seq_cst 이유:**  
Session Release Thread가 `nowInProcessingRecvPacket.load(seq_cst)`로 확인할 때,  
Logic Worker의 `store(false, seq_cst)`보다 먼저 읽힐 수 없음을 보장하기 위해  
`memory_order_seq_cst`를 사용한다. `relaxed`를 쓰면 CPU 재정렬로 오탐이 발생할 수 있다.

---

## 4. Retransmission Thread 상세

### 역할

일정 주기마다 재전송 목록을 순회하며 타임아웃된 패킷을 재전송한다.  
재전송 횟수가 한계를 초과하면 세션을 강제 종료한다.

### 코드 해석

```cpp
void MultiSocketRUDPCore::RunRetransmissionThread(
    const std::stop_token& stopToken, ThreadIdType threadId)
{
    auto& myList  = sendPacketInfoList[threadId];
    auto& myLock  = *sendPacketInfoListLock[threadId];
    TickSet tickSet;

    while (!stopToken.stop_requested()) {
        // ① 타임아웃된 항목 수집 (Lock 보유 시간 최소화)
        std::vector<SendPacketInfo*> copyList;
        {
            std::scoped_lock lock(myLock);
            for (auto* info : myList) {
                if (info->retransmissionTimeStamp > tickSet.nowTick) continue;
                if (info->isErasedPacketInfo.load(acquire))          continue;
                info->AddRefCount();    // Free 시 삭제되지 않도록
                copyList.push_back(info);
            }
        }
        // Lock 해제

        // ② Lock 없이 재전송 (시간이 걸리는 작업)
        for (auto* info : copyList) {
            // Lock 해제 중 ACK가 와서 이미 제거됐을 수 있음
            if (info->isErasedPacketInfo.load(acquire)) {
                SendPacketInfo::Free(info);
                continue;
            }

            if (++info->retransmissionCount >= maxPacketRetransmissionCount) {
                LOG_DEBUG(std::format("Max retransmission count. SessionId={}",
                    info->owner->GetSessionId()));
                info->owner->DoDisconnect();
                SendPacketInfo::Free(info);
                continue;
            }

            // 재전송: 기존 버퍼 그대로 재사용 (이미 암호화됨)
            core.SendPacket(info);

            // 다음 재전송 타임스탬프 갱신
            info->retransmissionTimeStamp =
                GetTickCount64() + retransmissionThreadSleepMs;

            SendPacketInfo::Free(info);
        }

        SleepRemainingFrameTime(tickSet, retransmissionThreadSleepMs);
    }
}
```

### `AddRefCount` / `Free` 설계

```cpp
struct SendPacketInfo {
    std::atomic<int> refCount = 0;

    void AddRefCount() {
        refCount.fetch_add(1, std::memory_order_acq_rel);
    }

    static void Free(SendPacketInfo* info) {
        if (info->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            // 마지막 참조 → 실제 메모리 풀 반환
            sendPacketInfoPool->Free(info);
        }
        // refCount > 1이면 다른 곳에서 아직 사용 중 → 반환하지 않음
    }
};
```

**참조 카운팅이 필요한 이유:**  
재전송 스레드가 `SendPacketInfo`를 `copyList`에 넣은 직후,  
Logic Worker가 ACK를 받아 `EraseSendPacketInfo()`를 호출할 수 있다.  
RefCount가 1보다 크면 메모리를 실제로 해제하지 않아 use-after-free를 방지한다.

---

## 5. Session Release Thread 상세

### 역할

`DoDisconnect()`에 의해 RELEASING 상태가 된 세션의 소켓을 닫고 풀에 반환한다.  
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

            // 안전 체크 ①: RIO Send 완료 대기
            bool isSending =
                session->GetSendContext().GetIOMode() == IO_MODE::IO_SENDING;

            // 안전 체크 ②: RecvLogic Worker 처리 완료 대기
            bool isProcessing =
                session->nowInProcessingRecvPacket.load(std::memory_order_seq_cst);

            if (isSending || isProcessing) {
                remainList.push_back(id);
                continue;
            }

            // 안전 확인 완료 → 실제 해제
            session->Disconnect();
            // → CloseSocket(), InitializeSession(), SetDisconnected()
            // → DisconnectSession(id) → unusedSessionIdList.push_back(id)
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
                    // RESERVED_SESSION_TIMEOUT_MS = 30000ms
                    sessionDelegate.AbortReservedSession(*session);
                    // → TryAbortReserved() CAS: RESERVED → RELEASING
                    // → CloseSocket(), InitializeSession()
                    // → DisconnectSession(id) → 풀 반환
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
  → session->DoDisconnect()  ← 서버 측에서 종료 감지
```

---

## 7. 스레드 시작 순서와 이유

```
1. Ticker::Start(timerTickMs)
   └─ 이유: 타이머 이벤트가 스레드 시작 전에 활성화될 수 있어야 함

2. 이벤트 핸들 생성 (for N스레드)
   └─ recvLogicThreadEventHandles[i] = CreateSemaphore(NULL, 0, LONG_MAX, NULL)
   └─ 이유: RecvLogic Worker가 시작 전에 핸들이 준비되어야 함

3. SESSION_RELEASE_THREAD 시작
   └─ 이유: IO Worker가 시작하기 전에 세션 해제 준비 완료

4. HEARTBEAT_THREAD 시작
   └─ 이유: 조기 연결된 세션이 없으므로 순서 무관, 조기 시작이 안전

5. IO_WORKER_THREAD × N 시작
   └─ 이유: 실제 I/O 처리의 핵심; RIO 큐 디큐 시작

6. RECV_LOGIC_WORKER_THREAD × N 시작
   └─ 이유: IO Worker의 Semaphore Release에 응답 준비

7. RETRANSMISSION_THREAD × N 시작
   └─ 이유: 이 시점에 sendPacketInfoList가 초기화됨

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
   └─ 이유: 소켓 에러 발생 → IO Worker가 에러 처리 → DoDisconnect 자동 호출
   └─ RIO 완료 큐에 에러 완료가 쌓임 → IO Worker가 처리 후 루프 종료

3. SetEvent(recvLogicThreadEventStopHandle)
   └─ 이유: Logic Worker들에게 종료 신호 (10초 후 잔여 패킷 처리 후 종료)

4. SetEvent(sessionReleaseStopEventHandle)
   └─ 이유: Release Thread에게 종료 신호

5. StopAllThreads()
   └─ RUDPThreadManager::RequestStop(각 그룹)
   └─ stop_token 신호 → jthread 자동 join

종료 순서가 중요한 이유:
  - IO Worker보다 먼저 Logic Worker를 종료하면: Semaphore에 신호가 쌓이지만 처리 안 됨
  - 세션 해제 전 스레드 종료: DoDisconnect 후 Disconnect가 호출 안 됨 → 세션 풀 고갈
  - Logger 이전에 다른 스레드 종료: 종료 중 발생한 로그 유실 가능
```

---

## 9. 스레드 간 공유 데이터 흐름

```
[IO Worker thread=0]
  │
  ├─ RIODequeueCompletion → OP_RECV 완료 감지
  │   → NetBuffer::Alloc() + memcpy(recvBuffer → newBuffer)
  │   → session.recvBufferList.Enqueue(newBuffer)
  │   → recvIOCompletedContextPool.Alloc() → init
  │   → recvIOCompletedContexts[0].Enqueue(context)
  │   → ReleaseSemaphore(recvLogicEventHandles[0], 1)    ← 깨움
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
  │   → session.recvBufferList.Dequeue(buffer)
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
      → core.EraseSendPacketInfo(info, threadId=0)      ← 아래 목록에서 제거
          → sendPacketInfoList[0].erase(listItor)
      → TryFlushPendingQueue()

[Retransmission Thread thread=0]
  │
  └─ sendPacketInfoList[0] 순회 (sendPacketInfoListLock[0] 보호)
      → isErasedPacketInfo 확인 (ACK 이미 받았으면 skip)
      → 타임아웃 확인
      → core.SendPacket(info)                           ← 재전송
      → retransmissionCount >= max → session.DoDisconnect()

[Session Release Thread]
  │
  ├─ WaitForMultipleObjects
  │   → sessionReleaseEventHandle 신호 (PushToDisconnectTargetSession에서 Set)
  │
  ├─ GetReleasingSession(id)
  │   → IO_SENDING 체크 (IO Worker 상태)
  │   → nowInProcessingRecvPacket 체크 (Logic Worker 상태)
  │
  └─ 안전 확인 후 session.Disconnect()
      → CloseSocket()
      → ForEachAndClearSendPacketInfoMap
          → core.EraseSendPacketInfo(info, threadId)    ← 각 스레드 목록에서 제거
      → OnReleased()
      → InitializeSession()
      → SetDisconnected()
      → sessionManager.ReleaseSession(id)
          → unusedSessionIdList.push_back(id)

[Heartbeat Thread]
  │
  └─ session.SendHeartbeatPacket()
      → HEARTBEAT_TYPE 패킷 전송
      → sendPacketInfoList[session.threadId]에 등록
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
