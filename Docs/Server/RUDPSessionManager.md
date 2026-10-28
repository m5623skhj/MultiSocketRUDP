# RUDPSessionManager

> **고정 크기 세션 풀 관리자.**  
> 서버 시작 시 `numOfSockets`개의 세션을 미리 생성하고,  
> 세션 객체를 재사용해 연결마다 세션 객체를 다시 할당하지 않는다. 목록·해시 집합과 세션 하위 자원에서는 런타임 할당이 발생할 수 있다.

---

## 목차

1. [설계 목적 — 풀 패턴](#1-설계-목적--풀-패턴)
2. [초기화 — `Initialize`](#2-초기화--initialize)
3. [세션 할당 — `AcquireSession`](#3-세션-할당--acquiresession)
4. [세션 반환 — `ReleaseSession`](#4-세션-반환--releasesession)
5. [함수 설명](#5-함수-설명)
6. [세션 조회](#6-세션-조회)
7. [연결 수 카운터](#7-연결-수-카운터)
8. [종료 순서 — 세 단계 안전 정리](#8-종료-순서--세-단계-안전-정리)
9. [이중 반환 방지 — `unusedSessionIdSet`](#9-이중-반환-방지--unusedsessionidset)

---

## 1. 설계 목적 — 풀 패턴

**세션 객체의 할당·해제 경계:**

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
| `ReleaseSession()` | 컨테이너 조작은 평균 O(1), 세션 초기화 비용 별도 | `mutex` |
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
bool RUDPSessionManager::ReleaseSession(SessionIdType sessionId);
```

1. ID 범위와 세션 포인터를 검사한다.
2. 상태가 `RELEASING` 또는 `RELEASING_BY_ABORT_RESERVED`인지 확인하고 종료 사유를 보관한다.
3. `unusedSessionIdListLock` 안에서 `unusedSessionIdSet.contains()`로 이중 반환을 거부한다.
4. `InitializeSession()`으로 세션을 초기화한 뒤 목록과 집합에 ID를 추가한다.
5. 원래 상태가 `RELEASING`인 연결 세션만 `DecrementConnectedCount()`로 통계를 갱신한다. 예약 중단은 연결 해제 통계에서 제외한다.

수명 정리·I/O 완료 대기는 이 함수 전에 release worker가 끝내야 한다. 목록 잠금만으로 사용 중인 세션을 안전하게 반환할 수 있는 것은 아니다.

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
- `RELEASING` 또는 `RELEASING_BY_ABORT_RESERVED` 상태 세션을 조회한다.

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
- CONNECTED 세션은 `NORMAL`, RESERVED 세션은 `BY_ABORT_RESERVED` 경로로
  각각 `RELEASING`, `RELEASING_BY_ABORT_RESERVED` 상태에 전달한다.
- 실제 소켓 close와 풀 반환은 release worker의 drain 절차가 수행한다.

### `ClearAllSessions`

```cpp
void ClearAllSessions();
```

모든 세션 리소스를 해제하고 세션 풀을 비운다. 관리 중인 모든 `RUDPSession` 객체를 삭제하고 초기화 상태를 해제한다.
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

// 두 해제 상태의 세션 접근 (Session Release Thread 전용)
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
  IsReleasing() = RELEASING || RELEASING_BY_ABORT_RESERVED
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
  → ReleaseSession()이 일반 RELEASING 상태를 반환할 때만 호출
  → RELEASING_BY_ABORT_RESERVED는 연결된 적이 없으므로 통계에서 제외
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

// ① 모든 활성 세션을 연결 여부에 맞는 해제 상태로 전환
CloseAllSessions();
for (auto* session : sessionList) {
    if (session->IsReserved()) {
        session->AbortReservedSession();
    } else if (session->IsConnected()) {
        session->DoDisconnect(DISCONNECT_REASON::NORMAL);
    }
}

// ② IO/logic/release worker를 유지해 모든 세션 drain 및 pool 반환 대기

// ③ 스레드 종료 (join)
StopAllThreads();

// ④ 실제 메모리 해제
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

목록과 집합은 같은 mutex 아래에서 갱신한다. `AcquireSession()`은 목록 앞에서 ID를 꺼내 집합에서 지우고, `ReleaseSession()`은 집합에 이미 있는 ID를 거부한 뒤 초기화·목록 추가·집합 추가를 수행한다. 실제 호출은 단일 release worker의 수명 계약을 따른다. 상태 검사와 객체 사용 사이까지 mutex 하나로 보호한다고 가정하지 않는다.

조회는 비소유 세션 포인터를 반환한다. 서버 종료와 재사용을 막는 별도 수명 보장이 필요하며 `IsUsingSession()` 조회만으로 포인터의 이후 수명이 고정되지 않는다.

---

## 관련 문서
- [[MultiSocketRUDPCore]] — Initialize 호출과 세션 관리자 연계
- [[SessionLifecycle]] — AcquireSession/ReleaseSession 호출 시점
- [[RUDPSession]] — sessionId 불변식 사용
- [[ThreadModel]] — HeartbeatThread와 Session Release 흐름
