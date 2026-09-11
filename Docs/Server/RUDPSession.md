# RUDPSession

> 콘텐츠 서버에서 실제로 상속해 사용하는 핵심 세션 클래스다.

---

## 목차

1. [콘텐츠 코드에서 직접 쓰는 API](#콘텐츠-코드에서-직접-쓰는-api)
2. [오버라이드 포인트](#오버라이드-포인트)
3. [최소 예시](#최소-예시)
4. [주의할 점](#주의할-점)
5. [내부 구현 이해 포인트](#내부-구현-이해-포인트)
6. [RTT와 재전송 timeout 전달](#rtt와-재전송-timeout-전달)

---

## 콘텐츠 코드에서 직접 쓰는 API

### `RegisterPacketHandler`

```cpp
void RegisterPacketHandler(const PacketId packetId, void (DerivedType::* func)(const PacketType&))
```

특정 `PacketId`에 대한 처리 함수를 등록한다. 패킷 수신 시 등록된 `func`가 호출된다.

| 파라미터 | 설명 |
|----------|------|
| `packetId` | 핸들러를 등록할 패킷의 ID |
| `func` | 패킷 수신 시 실행될 `DerivedType`의 멤버 함수 포인터 |

#### 사용 예시

```cpp
RegisterPacketHandler<Player, Ping>(
    static_cast<PacketId>(PACKET_ID::PING),
    &Player::OnPing);
```
### 패킷 송신

```cpp
bool SendPacket(IPacket& packet);
```

패킷을 송신 큐에 추가하여 전송을 요청한다.

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `packet` | `IPacket&` | 송신할 패킷 객체 |

| 반환값 | 조건 |
|--------|------|
| `true` | 송신 작업 성공 |
| `false` | 세션이 연결되지 않았거나 큐가 가득 참 |
### 연결 종료 요청

```cpp
void DoDisconnect(const DISCONNECT_REASON disconnectSession);
```

현재 `DoDisconnect()`는 반드시 `DISCONNECT_REASON` 인자를 받는다.  
예전 무인자 호출 예제는 현재 헤더 기준으로 맞지 않는다.

### 기본 조회

```cpp
[[nodiscard]]
SessionIdType GetSessionId() const;
[[nodiscard]]
bool IsConnected() const;
[[nodiscard]]
bool IsReserved() const;
[[nodiscard]]
bool IsReleasing() const;
[[nodiscard]]
SESSION_STATE GetSessionState() const;
DISCONNECT_REASON GetDisconnectedReason() const;
```

`GetDisconnectedReason()`을 제외한 위 기본 조회 함수에는 `[[nodiscard]]`가 있다.

> 반환값을 무시하면 컴파일 경고가 발생한다. 호출 측에서 반드시 검사해야 한다.

---

## 오버라이드 포인트

```cpp
void OnConnected() override;
void OnDisconnected() override;
void OnReleased() override;
```

### 의미

- `OnConnected()`: CONNECT 수락 직후
- `OnDisconnected()`: 사용자 수신 로직이 끝난 뒤 `BeginIOShutdown()`에서 socket을 닫기 직전
- `OnReleased()`: 풀 반환 직전, 재사용 전 상태 초기화 지점

---

## RegisterPacketHandler

```cpp
void RegisterPacketHandler(const PacketId packetId, void (DerivedType::* func)(const PacketType&))
```

특정 패킷 ID에 대응하는 핸들러 함수를 등록한다. 패킷 수신 시 지정된 멤버 함수가 호출된다.

| 파라미터 | 설명 |
|----------|------|
| `packetId` | 등록할 패킷의 식별자 |
| `func` | 패킷 처리 멤버 함수 포인터 |

> **주의:** `PacketType`은 반드시 `IPacket`을 상속받아야 한다.

## SendPacket

```cpp
bool SendPacket(IPacket& packet);
```

세션을 통해 패킷을 전송한다.

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `packet` | `IPacket&` | 전송할 패킷 객체 |

| 반환값 | 조건 |
|--------|------|
| `true` | 패킷 전송 성공 |
| `false` | 세션이 연결되지 않았거나 전송 실패 |

## 최소 예시

```cpp
class Player final : public RUDPSession
{
public:
    explicit Player(MultiSocketRUDPCore& inCore);

private:
    void OnConnected() override;
    void OnDisconnected() override;
    void OnReleased() override;

    void OnPing(const Ping& packet);
};

Player::Player(MultiSocketRUDPCore& inCore)
    : RUDPSession(inCore)
{
    RegisterPacketHandler<Player, Ping>(
        static_cast<PacketId>(PACKET_ID::PING),
        &Player::OnPing);
}

void Player::OnPing(const Ping&)
{
    Pong pong;
    SendPacket(pong);
}
```
## 주의할 점

### 1. `OnDisconnected()`는 정리 훅이다

이 구간은 이미 종료 흐름에 들어간 뒤다.  
새로운 일반 패킷 송신이나 긴 블로킹 작업을 넣는 용도로 쓰지 않는 편이 안전하다.

`DoDisconnect()`가 `RELEASING` 전이와 release queue 등록을 수행하는 즉시 호출되는 훅은 아니다. release worker는 `pendingRecvLogic`과 처리 중 플래그가 0이 된 뒤 `BeginIOShutdown()`에서 훅을 한 번 호출하며, 이어서 소켓을 닫아 신규 I/O 등록을 차단한다. 이후 기존 send/receive 작업과 `activeIOCompletions`가 drain될 때까지 세션은 `RELEASING` 상태로 유지된다.

### 2. `OnReleased()`에서는 상태 초기화만 하는 편이 낫다

세션은 이후 풀에서 재사용된다. 멤버 변수 초기화 지점으로 이해하는 것이 맞다.

### 3. 다른 세션을 `core.GetUsingSession()`으로 직접 조회하는 예제는 현재 공개 API 기준으로 맞지 않는다

문서에서 이 패턴을 콘텐츠 코드 예제로 쓰지 않는다.

### 4. 강제 종료는 아래처럼 호출한다

```cpp
DoDisconnect(DISCONNECT_REASON::BY_ERROR);
```

---

## 내부 구현 이해 포인트

- 송신은 즉시 소켓 호출이 아니라 코어 전송 경로를 거친다.
- ACK와 재전송은 세션 내부 송신 상태와 코어 스레드 모델이 함께 관리한다.
- 수신 순서 보장, heartbeat, ACK 생성은 내부 로직이 담당한다.

이 문서는 콘텐츠 확장 관점만 남기고, 오래된 내부 예제는 제거한다.

---

## RTT와 재전송 timeout 전달

### `OnRttSample`

```cpp
void OnRttSample(std::chrono::steady_clock::duration sample);
```

유효한 RTT 샘플을 사용하여 SRTT(Smoothed Round Trip Time), RTTVAR(Round Trip Time Variation), RTO(Retransmission TimeOut)를 갱신한다.

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `sample` | `std::chrono::steady_clock::duration` | 재전송된 적이 없는 패킷으로부터 측정된 RTT |

**전제 조건**:
- `sample`은 재전송되지 않은 패킷으로부터 측정된 유효한 값이어야 한다.

`OnRetransmissionTimeout()`은 estimator의 backoff가 실제 적용된 경우에만 `flowManager.OnTimeout()`을 호출한다. 현재 RTO는 `GetRetransmissionTimeoutMs()`로 조회한다. 계산식과 동시성 규칙은 [[RetransmissionTimeoutEstimator]]에 정리한다.

### `GetRetransmissionTimeoutMs`

```cpp
[[nodiscard]]
unsigned int GetRetransmissionTimeoutMs() const noexcept;
```

현재 세션 estimator가 계산한 RTO를 밀리초 단위로 반환한다. `std::atomic_uint`에 캐시된 값을 읽으므로 estimator mutex를 획득하지 않는다.

> 반환값을 무시하면 컴파일 경고가 발생한다. 호출 측에서 반드시 검사해야 한다.

---

## 관련 문서

- [[GettingStarted]] - 최소 서버 구축
- [[MultiSocketRUDPCore]] - 서버 공개 API
- [[FlowController]] - 흐름 제어 개념
- [[RetransmissionTimeoutEstimator]] - SRTT/RTTVAR 기반 RTO 계산
- [[SendPacketInfo]] - RTT 표본 유효성 추적
