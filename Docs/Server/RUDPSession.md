# RUDPSession

> 콘텐츠 서버에서 실제로 상속해 사용하는 핵심 세션 클래스다.

---

## 콘텐츠 코드에서 직접 쓰는 API

### 패킷 핸들러 등록

```cpp
RegisterPacketHandler<Player, Ping>(
    static_cast<PacketId>(PACKET_ID::PING),
    &Player::OnPing);
```

### 패킷 송신

```cpp
bool SendPacket(IPacket& packet);
```

### 연결 종료 요청

```cpp
void DoDisconnect(const DISCONNECT_REASON disconnectSession);
```

현재 `DoDisconnect()`는 반드시 `DISCONNECT_REASON` 인자를 받는다.  
예전 무인자 호출 예제는 현재 헤더 기준으로 맞지 않는다.

### 기본 조회

```cpp
SessionIdType GetSessionId() const;
bool IsConnected() const;
bool IsReserved() const;
bool IsReleasing() const;
SESSION_STATE GetSessionState() const;
DISCONNECT_REASON GetDisconnectedReason() const;
```

---

## 오버라이드 포인트

```cpp
void OnConnected() override;
void OnDisconnected() override;
void OnReleased() override;
```

### 의미

- `OnConnected()`: CONNECT 수락 직후
- `OnDisconnected()`: 릴리즈 요청 진입 직후
- `OnReleased()`: 풀 반환 직전, 재사용 전 상태 초기화 지점

---

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

---

## 주의할 점

### 1. `OnDisconnected()`는 정리 훅이다

이 구간은 이미 종료 흐름에 들어간 뒤다.  
새로운 일반 패킷 송신이나 긴 블로킹 작업을 넣는 용도로 쓰지 않는 편이 안전하다.

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


---

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

## 관련 문서

- [[GettingStarted]] - 최소 서버 구축
- [[MultiSocketRUDPCore]] - 서버 공개 API
- [[FlowController]] - 흐름 제어 개념
