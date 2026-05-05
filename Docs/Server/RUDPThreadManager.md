# RUDPThreadManager

> **`std::jthread` 그룹을 `THREAD_GROUP` 열거값으로 관리하는 스레드 관리자.**  
> C++20 `jthread`의 협력적 종료(`stop_token`)와 자동 `join` 기능을 활용해  
> 스레드 생명주기를 안전하게 관리한다.

---

## 목차

1. [THREAD_GROUP 열거형](#1-thread_group-열거형)
2. [내부 구조](#2-내부-구조)
3. [함수 설명](#3-함수-설명)
4. [스레드 시작 — StartThreads](#4-스레드-시작--startthreads)
5. [스레드 종료 — StopThreadGroup / StopAllThreads](#5-스레드-종료--stopthreadgroup--stopallthreads)
6. [jthread 선택 이유](#6-jthread-선택-이유)
7. [각 그룹의 stop_token 처리](#7-각-그룹의-stop_token-처리)

---

## 1. THREAD_GROUP 열거형

```cpp
enum class THREAD_GROUP : unsigned char {
    IO_WORKER_THREAD         = 0,   // RIO 완료 큐 폴링
    RECV_LOGIC_WORKER_THREAD = 1,   // 패킷 타입 분기, 핸들러 호출
    RETRANSMISSION_THREAD    = 2,   // 미ACK 패킷 재전송
    SESSION_RELEASE_THREAD   = 3,   // RELEASING 세션 정리
    HEARTBEAT_THREAD         = 4,   // 하트비트 전송, 예약 세션 타임아웃
};
```

---

## 2. 내부 구조

```cpp
class RUDPThreadManager {
    std::map<THREAD_GROUP, std::vector<std::jthread>> threadGroups;

public:
    void StartThreads(const THREAD_GROUP threadGroup, std::function<void(std::stop_token, unsigned char)> threadFunction, const uint8_t numOfThreads);
    void StopThreadGroup(const THREAD_GROUP threadGroup);
    void StopAllThreads();
};
```

---

## 3. 함수 설명

#### `void StartThreads(const THREAD_GROUP threadGroup, std::function<void(std::stop_token, unsigned char)> threadFunction, const uint8_t numOfThreads)`
- 지정 그룹에 `numOfThreads` 개수만큼 `std::jthread`를 생성한다.
- 각 스레드는 `stop_token`과 인덱스를 인자로 받아 동작한다.

#### `void StopThreadGroup(const THREAD_GROUP threadGroup)`
- 특정 그룹의 모든 스레드에 stop을 요청하고 정리한다.

#### `void StopAllThreads()`
- 전체 그룹을 순회하며 모든 스레드를 중단한다.

---

## 4. 스레드 시작 — `StartThreads`

```cpp
void RUDPThreadManager::StartThreads(
    const THREAD_GROUP threadGroup,
    std::function<void(std::stop_token, unsigned char)> threadFunction,
    const uint8_t numOfThreads)
{
    auto& vec = threadGroups[threadGroup];

    for (uint8_t i = 0; i < numOfThreads; ++i) {
        vec.emplace_back(threadFunction, static_cast<unsigned char>(i));
    }
}
```

**`MultiSocketRUDPCore::RunAllThreads`에서의 호출:**

```cpp
// 시작 순서 (순서 중요)
threadManager->StartThreads(SESSION_RELEASE_THREAD,
    [this](std::stop_token st, unsigned char) { RunSessionReleaseThread(st); },
    1);

threadManager->StartThreads(HEARTBEAT_THREAD,
    [this](std::stop_token st, unsigned char) { RunHeartbeatThread(st); },
    1);

threadManager->StartThreads(IO_WORKER_THREAD,
    [this](std::stop_token st, unsigned char id) { RunIOWorkerThread(st, id); },
    numOfWorkerThread);

threadManager->StartThreads(RECV_LOGIC_WORKER_THREAD,
    [this](std::stop_token st, unsigned char id) { RunRecvLogicWorkerThread(st, id); },
    numOfWorkerThread);

threadManager->StartThreads(RETRANSMISSION_THREAD,
    [this](std::stop_token st, unsigned char id) { RunRetransmissionThread(st, id); },
    numOfWorkerThread);

Sleep(1000);  // 모든 스레드 안정화 대기

sessionBroker->Start(...);  // 마지막에 클라이언트 수락 시작
```

---

## 5. 스레드 종료 — `StopThreadGroup` / `StopAllThreads`

```cpp
void RUDPThreadManager::StopThreadGroup(THREAD_GROUP group)
{
    auto it = threadGroups.find(group);
    if (it == threadGroups.end()) return;

    for (auto& t : it->second) {
        t.request_stop();   // stop_token 신호
    }
    // jthread 소멸 → join() 자동 (블로킹)
    it->second.clear();
}

void RUDPThreadManager::StopAllThreads()
{
    // 모든 그룹에 stop 신호 (동시)
    {
        for (auto& [group, vec] : threadGroups) {
            for (auto& t : vec) {
                t.request_stop();
            }
        }
    }

    // 순서대로 join (소멸자 방식)
    threadGroups.clear();   // 모든 jthread 소멸 → join
}
```

**`StopAllThreads`에서 `request_stop()`을 먼저 모두 보내는 이유:**

```
순서대로 stop + join하면:
  StopGroup(IO_WORKER) → join 대기
  그 동안 RECV_LOGIC_WORKER는 아직 실행 중
  → IO_WORKER가 RECV_LOGIC에 신호를 보낼 수 없는 상황

모두에게 먼저 stop 신호:
  → 모든 그룹이 종료 준비를 동시에 시작
  → 이후 join으로 완료 대기
  → 최단 시간에 전체 종료
```

---

## 6. jthread 선택 이유

```cpp
// std::thread (C++11)
std::thread t([](){ while (!stopFlag) {} });
t.join();   // 수동 join 필요, 잊으면 terminate()

// std::jthread (C++20)
std::jthread t([](std::stop_token st){ while (!st.stop_requested()) {} });
// 소멸 시 request_stop() + join() 자동
```

| 기능 | `std::thread` | `std::jthread` |
|------|---------------|----------------|
| 소멸자 자동 join | ❌ (terminate) | ✅ |
| 협력적 종료 신호 | ❌ (수동 flag) | ✅ (`stop_token`) |
| `request_stop()` | ❌ | ✅ |

**실제 종료 코드 비교:**

```cpp
// thread 방식 (RUDPThreadManager 이전)
bool stopFlag = false;
std::thread t([&stopFlag]() {
    while (!stopFlag) { /* ... */ }
});
stopFlag = true;
t.join();   // 잊으면 크래시

// jthread 방식 (현재)
std::jthread t([](std::stop_token st) {
    while (!st.stop_requested()) { /* ... */ }
});
// 스코프 벗어날 때 또는 clear()에서 자동 처리
```

---

## 7. 각 그룹의 stop_token 처리

### IO_WORKER_THREAD

```cpp
void RunIOWorkerThread(std::stop_token stopToken, ThreadIdType threadId)
{
    while (!stopToken.stop_requested()) {
        RIORESULT results[1024];
        ULONG count = rioManager->DequeueCompletions(threadId, results, 1024);
        for (ULONG i = 0; i < count; ++i) {
            ioHandler->IOCompleted(GetIOCompletedContext(results[i]),
                                   results[i].BytesTransferred, threadId);
        }
        // sleep_for (USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME)
    }
}
```

`stop_requested()`는 폴링 루프의 조건문에서 체크. `CloseAllSessions()`에 의한 소켓 닫힘으로  
IO 완료가 에러 상태로 유입되어 루프가 자연스럽게 처리됨.

### RECV_LOGIC_WORKER_THREAD

```cpp
void RunRecvLogicWorkerThread(std::stop_token stopToken, ThreadIdType threadId)
{
    HANDLE handles[2] = {
        recvLogicThreadEventHandles[threadId],  // Semaphore
        recvLogicThreadEventStopHandle           // ManualResetEvent (Stop 신호)
    };

    while (!stopToken.stop_requested()) {
        switch (WaitForMultipleObjects(2, handles, FALSE, INFINITE)) {
        case WAIT_OBJECT_0:
            OnRecvPacket(threadId);
            break;
        case WAIT_OBJECT_0 + 1:
            Sleep(LOGIC_THREAD_STOP_SLEEP_TIME);  // 10초 대기 후 잔여 처리
            OnRecvPacket(threadId);
            return;
        }
    }
}
```

`stop_token`으로는 이 스레드가 즉시 종료하지 않는다.  
`recvLogicThreadEventStopHandle`이 실제 종료 신호다.  
`StopServer`에서 `SetEvent(recvLogicThreadEventStopHandle)` 호출.

### RETRANSMISSION_THREAD

```cpp
void RunRetransmissionThread(std::stop_token stopToken, ThreadIdType threadId)
{
    TickSet tickSet;
    while (!stopToken.stop_requested()) {
        // ... 재전송 로직 ...
        SleepRemainingFrameTime(tickSet, retransmissionThreadSleepMs);
    }
}
```

`stop_requested()` + sleep 패턴. sleep 중에는 최대 `retransmissionThreadSleepMs`ms 지연.

### SESSION_RELEASE_THREAD / HEARTBEAT_THREAD

```cpp
while (!stopToken.stop_requested()) {
    WaitForMultipleObjects(2, handles, FALSE, INFINITE);
    // ...
}
```

이벤트 대기 방식. `stop_requested()`와 별도 이벤트(Stop 이벤트) 모두 확인.

---

## 관련 문서
- [[ThreadModel]] — 각 스레드 그룹 동작 상세
- [[MultiSocketRUDPCore]] — RunAllThreads / StopServer에서 사용
