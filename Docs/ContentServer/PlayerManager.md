# PlayerManager

> **샘플 콘텐츠 서버의 `PlayerId`·`SessionId` 양방향 조회를 제공하는 싱글톤이다.**  
> 두 `unordered_map`에 `Player*`를 비소유 참조로 보관한다.

---

## 목차

1. [싱글톤 접근](#1-싱글톤-접근)
2. [등록과 제거](#2-등록과-제거)
3. [조회](#3-조회)
4. [동시성 및 수명 주의](#4-동시성-및-수명-주의)

---

## 1. 싱글톤 접근

### `GetInst`

```cpp
static PlayerManager& GetInst();
```

함수 지역 static 인스턴스를 반환한다.

---

## 2. 등록과 제거

### `AddPlayer`

```cpp
void AddPlayer(PlayerIdType playerId, Player* player);
```

`playerId → Player*`와 `player->GetSessionId() → Player*` 매핑을 각각 추가한다.

> **전제 조건:** `player`는 `nullptr`이 아니어야 하며, 등록하는 두 ID는 기존 map에 없어야 한다. 현재 구현은 `insert` 결과를 검사하거나 기존 항목을 교체하지 않는다.

### `ErasePlayer`

```cpp
void ErasePlayer(PlayerIdType playerId);
```

플레이어 ID로 세션 ID를 찾은 뒤 두 map에서 해당 항목을 제거한다. 플레이어 ID를 찾지 못하면 아무 작업도 하지 않는다.

### `ErasePlayerBySessionId`

```cpp
void ErasePlayerBySessionId(SessionIdType sessionId);
```

세션 ID로 플레이어 ID를 찾은 뒤 두 map에서 해당 항목을 제거한다. 세션 ID를 찾지 못하면 아무 작업도 하지 않는다.

---

## 3. 조회

### `FindPlayer`

```cpp
Player* FindPlayer(PlayerIdType playerId);
```

player ID에 대응하는 포인터를 반환한다. 항목이 없으면 `nullptr`을 반환한다.

### `FindPlayerBySessionId`

```cpp
Player* FindPlayerBySessionId(SessionIdType sessionId);
```

세션 ID에 대응하는 포인터를 반환한다. 항목이 없으면 `nullptr`을 반환한다.

---

## 4. 동시성 및 수명 주의

- 플레이어 map과 세션 map은 각각 별도 `std::shared_mutex`로 보호한다.
- 단일 map의 조회는 shared lock, 변경은 unique lock을 사용한다.
- 두 map의 등록·제거는 하나의 transaction으로 잠기지 않는다. 동시에 조회하면 한쪽 map만 갱신된 중간 상태를 관찰할 수 있다.
- 조회 함수는 lock을 해제한 뒤 비소유 `Player*`를 반환한다. 호출 측은 반환 포인터를 사용하는 동안 `Player`가 해제되지 않는다는 별도 수명 보장을 가져야 한다.
- `ErasePlayer*()`는 조회 lock과 제거 lock 사이에 간격이 있으므로, 같은 `Player`를 동시에 제거·해제하는 흐름과 함께 호출하면 안전하지 않다.

> 이 클래스의 map lock은 컨테이너 자체만 보호한다. `Player` 객체의 수명이나 멤버 접근까지 스레드 안전하게 만들지는 않는다.

---

## 관련 문서

- [[ContentServerGuide]] - 샘플 콘텐츠 서버 구성
- [[RUDPSession]] - `Player` 기반 세션 생명주기
- [[SessionLifecycle]] - 연결 해제와 세션 반환 시점
