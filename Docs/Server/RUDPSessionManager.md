# RUDPSessionManager

> **고정 크기 세션 풀 관리자.**  
> 서버 시작 시 `numOfSockets`개의 세션을 미리 생성하고,  
> O(1) 할당/반환/조회로 런타임 동적 할당을 완전히 제거한다.

---

## 목차

1. [설계 목적 — 풀 패턴](#1-설계-목적--풀-패턴)
2. [초기화 — Initialize](#2-초기화--initialize)
3. [세션 할당 — AcquireSession](#3-세션-할당--acquiresession)
4. [세션 반환 — ReleaseSession](#4-세션-반환--releasesession)
5. [함수 설명](#5-함수-설명)
6. [세션 조회](#6-세션-조회)
7. [연결 수 카운터](#7-연결-수-카운터)
8. [종료 순서 — 세 단계 안전 정리](#8-종료-순서--세-단계-안전-정리)
9. [이중 반환 방지 — unusedSessionIdSet](#9-이중-반환-방지--unusedsessionidset)

---

## 1. 설계 목적 — 풀 패턴

**런타임 `new`/`delete` 없음:**

```
서버 시작 시:
  for i in 0..numOfSockets:
    session = factoryFunc(core)   ← 단 한 번 할당
    sessionList[i] = session      ← 인덱스 = sessionId

운영 중:
  연결: AcquireSession() → O(1) pop_front (list)
  해제: ReleaseSession() → O(1) push_back (list)
  조회: GetUsingSession(id) → O(1) 배열 인덱스

서버 종료 시:
  delete session                  ← 단 한 번 해제
```

**성능:**

| 연산 | 시간복잡도 | 동기화 |
|------|-----------|--------|
| `AcquireSession()` | O(1) | `mutex` |
| `ReleaseSession()` | O(1) | `mutex` |
| `GetUsingSession(id)` | O(1) | 없음 |
| `GetReleasingSession(id)` | O(1) | 없음 |

---

## 2. 초기화 — `Initialize`

```cpp
bool RUDPSessionManager::Initialize(
    BYTE numOfWorkerThread,
    SessionFactoryFunc&& factoryFunc
)
```

```cpp
{
    for (size_t i = 0; i < maxSessionSize; ++i) {
        // ① 콘텐츠 팩토리 호출 (콘텐츠 서버가 구현한 람다)
        RUDPSession* session = factoryFunc(core);
        if (!session) {
            LOG_ERROR(std::format("Session factory returned nullptr at index {}", i));
            return false;
        }

        // ② sessionId 설정 = 인덱스 (불변식: sessionList[id] = session with id==i)
        sessionDelegate.SetSessionId(*session, static_cast<SessionIdType>(i));

        // ③ 스레드 배정 (RIO 완료 큐 분산)
        sessionDelegate.SetThreadId(*session, static_cast<ThreadIdType>(i % numOfWorkerThread));
        // → 세션 0,N,2N,... → threadId=0
        // → 세션 1,N+1,2N+1,... → threadId=1

        // ④ 풀에 등록
        sessionList.emplace_back(session);
        unusedSessionIdList.emplace_back(static_cast<SessionIdType>(i));
        unusedSessionIdSet.emplace(static_cast<SessionIdType>(i));
    }

    LOG_DEBUG(std::format("Session pool initialized. Size={}", maxSessionSize));
    return true;
}
```

**`sessionId == sessionList 인덱스` 불변식의 의미:**

```cpp
// GetUsingSession 구현
RUDPSession* GetUsingSession(SessionIdType id) const {
    if (id >= sessionList.size()) return nullptr;
    auto* session = sessionList[id];  // O(1) 인덱스 접근
    return session->IsUsingSession() ? session : nullptr;
}
```

인덱스 검색 없이 `id`로 직접 배열에 접근하므로 O(1) 보장.

---

## 3. 세션 할당 — `AcquireSession`

```cpp
RUDPSession* RUDPSessionManager::AcquireSession()
```

```cpp
{
    std::scoped_lock lock(unusedSessionIdListLock);

    if (unusedSessionIdList.empty()) {
        LOG_ERROR("Session pool exhausted");
        return nullptr;
    }

    SessionIdType id = unusedSessionIdList.front();
    unusedSessionIdList.pop_front();
    unusedSessionIdSet.erase(id);

    // 연결 카운터 증가는 TryConnect 성공 후에 (아직 RESERVED)
    return sessionList[id];
}
```

**`std::list` 선택 이유:**

```
vector: pop_front() → O(N) (앞 요소 제거 후 나머지 이동)
list:   pop_front() → O(1), push_back() → O(1)

빈번한 pop_front + push_back이 있으므로 list가 적합하다.
```

---

## 4. 세션 반환 — `ReleaseSession`

```cpp
bool RUDPSessionManager::ReleaseSession(SessionIdType sessionId)
```

```cpp
{
    std::scoped_lock lock(unusedSessionIdListLock);

    // 이중 반환 방지: 이미 unusedSessionIdSet에 있으면 반환
    if (!unusedSessionIdSet.insert(sessionId).second) {
        LOG_ERROR(std::format(
            "Session {} already in unused pool (double release attempt)", sessionId));
        return false;
    }

    unusedSessionIdList.push_back(sessionId);

    // 연결 카운터 감소
    if (connectedUserCount > 0) {
        --connectedUserCount;
    }

    return true;
}
```

**이중 반환이 발생할 수 있는 상황:**

```
1. DoDisconnect()가 두 스레드에서 동시에 호출됨
   → TryTransitionToReleasing CAS로 하나만 성공
   → 그러나 만약 둘 다 성공한다면 ReleaseSession이 두 번 호출될 수 있음

2. AbortReservedSession과 DoDisconnect가 경쟁
   → TryAbortReserved vs TryTransitionToReleasing CAS 경쟁
   → 하나만 성공하지만 방어 코드로 이중 반환을 막음
```

**`unusedSessionIdSet`의 역할:**

```
unusedSessionIdList: FIFO 반환 순서 유지 (list)
unusedSessionIdSet:  O(1) 중복 검사 (unordered_set)

두 자료구조를 동기화해서 유지:
  AcquireSession: list.pop_front() + set.erase()
  ReleaseSession: set.insert() 성공 시 list.push_back()
```

---

## 5. 함수 설명

#### `RUDPSessionManager(unsigned short inMaxSessionSize, MultiSocketRUDPCore& inCore, ISessionDelegate& inSessionDelegate)`
- 최대 세션 수, 코어, 세션 delegate를 받아 세션 풀 관리자를 구성한다.
- 세션 풀 크기와 의존성이 생성 시점에 확정되어야 하므로 필수 인자가 있는 생성자로 문서화한다.

#### `bool Initialize(BYTE inNumOfWorkerThreads, SessionFactoryFunc&& factory)`
- 세션 풀을 실제로 생성하고 각 세션에 `sessionId`와 `threadId`를 배정한다.

#### `RUDPSession* AcquireSession()`
- 재사용 가능한 세션을 하나 확보한다.

#### `bool ReleaseSession(SessionIdType sessionId)`
- 해제 완료된 세션을 unused 목록으로 되돌린다.

#### `RUDPSession* GetUsingSession(SessionIdType sessionId)`
#### `const RUDPSession* GetUsingSession(SessionIdType sessionId) const`
- 사용 중인 세션을 조회한다.

#### `RUDPSession* GetReleasingSession(SessionIdType sessionId) const`
- RELEASING 상태 세션을 조회한다.

#### `unsigned short GetNowSessionCount() const`
- 현재 연결 세션 수를 반환한다.

#### `unsigned int GetAllConnectedCount() const`
- 누적 연결 수를 반환한다.

#### `unsigned int GetAllDisconnectedCount() const`
- 누적 해제 수를 반환한다.

#### `unsigned int GetAllDisconnectedByRetransmissionCount() const`
- 재전송 한도 초과로 종료된 누적 수를 반환한다.

#### `unsigned short GetUnusedSessionCount() const`
- 재사용 가능한 세션 수를 반환한다.

#### `bool IsInitialized() const`
- 세션 매니저 초기화 여부를 반환한다.

#### `void CloseAllSessions()`
- 모든 활성 세션 소켓을 닫는다.

#### `void ClearAllSessions()`
- 세션 객체 메모리를 정리한다.

#### `void IncrementConnectedCount()`
- 성공 연결 시 통계를 증가시킨다.

#### `void DecrementConnectedCount(const DISCONNECT_REASON disconnectedReason)`
- 연결 해제 시 현재/누적 통계를 갱신한다.
- 해제 사유에 따라 retransmission 종료 카운트도 함께 반영한다.

#### `void HeartbeatCheck(const unsigned long long now) const`
- heartbeat 및 reserved timeout 관점에서 세션 상태를 점검한다.

---

## 6. 세션 조회

```cpp
// CONNECTED 또는 RESERVED 세션 접근 (콘텐츠 서버 API)
RUDPSession* GetUsingSession(SessionIdType sessionId) const
{
    if (sessionId >= sessionList.size()) return nullptr;
    auto* session = sessionList[sessionId];
    return session->IsUsingSession() ? session : nullptr;
}

// RELEASING 세션 접근 (Session Release Thread 전용)
RUDPSession* GetReleasingSession(SessionIdType sessionId) const
{
    if (sessionId >= sessionList.size()) return nullptr;
    auto* session = sessionList[sessionId];
    return session->IsReleasing() ? session : nullptr;
}

```

**`GetUsingSession` vs `GetReleasingSession` 분리 이유:**

```
GetUsingSession:
  IsUsingSession() = RESERVED || CONNECTED
  → 콘텐츠 서버에서 다른 세션에 패킷 보낼 때 사용
  → RELEASING은 이미 DoDisconnect됨 → 전송 의미 없음

GetReleasingSession:
  IsReleasing() = RELEASING만
  → Session Release Thread에서 해제할 세션 찾을 때
  → CONNECTED/RESERVED를 실수로 해제하면 안 됨
```

---

## 7. 연결 수 카운터

```cpp
std::atomic_uint16_t connectedUserCount{ 0 };
std::atomic_uint32_t allConnectedCount{ 0 };
std::atomic_uint32_t allDisconnectedCount{ 0 };
std::atomic_uint32_t allDisconnectedByRetransmissionCount{ 0 };

void IncrementConnectedCount();
void DecrementConnectedCount(const DISCONNECT_REASON disconnectedReason);
unsigned short GetNowSessionCount() const;
unsigned int GetAllConnectedCount() const;
unsigned int GetAllDisconnectedCount() const;
unsigned int GetAllDisconnectedByRetransmissionCount() const;
```

**증가/감소 시점:**

```
IncrementConnectedCount():
  → ProcessByPacketType: CONNECT_TYPE 처리 성공 후
  → TryConnect() 반환 true 직후

DecrementConnectedCount():
  → ReleaseSession() 내부에서 자동 감소
  → (pool 반환 시 연결 수 감소)
```

**`MultiSocketRUDPCore::GetNowSessionCount()`:**

```cpp
unsigned short MultiSocketRUDPCore::GetNowSessionCount() const {
    return sessionManager->GetNowSessionCount();
}
```

---

## 8. 종료 순서 — 세 단계 안전 정리

```cpp
// MultiSocketRUDPCore::StopServer에서의 정리 순서:

// ① 모든 세션 소켓 닫기
CloseAllSessions();
for (auto* session : sessionList) {
    sessionDelegate.CloseSocket(*session);
    // → IO Worker의 RIO 작업에 에러 유발 → IOCompleted에서 에러 처리
}

// ② 스레드 종료 (join)
StopAllThreads();

// ③ 실제 메모리 해제
ClearAllSessions();
{
    unusedSessionIdList.clear();
    unusedSessionIdSet.clear();
    connectedUserCount = 0;

    for (auto* session : sessionList) {
        delete session;   // 콘텐츠 클래스의 소멸자 실행
    }
    sessionList.clear();
}
```

**순서가 중요한 이유:**

```
① 먼저 소켓을 닫아야:
  → IO Worker가 에러 완료를 감지 → 루프 종료 조건 충족

② 스레드 종료 후에:
  → 세션 포인터를 접근하는 스레드가 없음을 보장

③ 마지막에 메모리 해제:
  → 스레드가 완전히 종료된 후 → use-after-free 없음
```

---

## 9. 이중 반환 방지 — `unusedSessionIdSet`

```cpp
// 이중 반환 시도 시나리오:
// 두 스레드가 동시에 ReleaseSession(5)를 호출

[스레드 A]                      [스레드 B]
scoped_lock lock                 scoped_lock lock (대기)
  set.insert(5) → {5} → 성공
  list.push_back(5)
unlock

                                 lock 획득
                                   set.insert(5) → 이미 있음 → 실패
                                   LOG_ERROR("double release")
                                   return false
                                 unlock

결과: id=5가 list에 1번만 들어감
```

**`set.insert()` 반환값 활용:**

```cpp
auto [it, inserted] = unusedSessionIdSet.insert(sessionId);
if (!inserted) {
    // 이미 set에 있음 = 이미 반환된 세션
    return false;
}
// insert 성공 → list에도 추가
unusedSessionIdList.push_back(sessionId);
```

---

## 관련 문서
- [[MultiSocketRUDPCore]] — Initialize 호출, GetUsingSession API
- [[SessionLifecycle]] — AcquireSession/ReleaseSession 호출 시점
- [[RUDPSession]] — sessionId 불변식 사용
- [[ThreadModel]] — HeartbeatThread와 Session Release 흐름
