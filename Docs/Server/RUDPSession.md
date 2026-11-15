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
bool SendUnreliablePacket(IPacket& packet);
```

`SendPacket()`은 ACK·순서 보장·재전송이 있는 신뢰 채널로, `SendUnreliablePacket()`은 최신성 우선 채널로 전송을 요청한다.

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `packet` | `IPacket&` | 송신할 패킷 객체 |

| 반환값 | 조건 |
|--------|------|
| `true` | 로컬 송신 경로에 수용됨. 원격 전달을 뜻하지 않음 |
| `false` | 미연결·종료, 직렬화·암호화 실패, 크기 초과 또는 신뢰성 pending 큐 등록 실패 |

`SendUnreliablePacket()`은 별도의 64비트 시퀀스와 `UNRELIABLE_SEND_TYPE`을 사용한다. ACK, CWND, 수신 재정렬, 재전송은 적용하지 않는다. 큐가 가득 찬 경우 가장 오래된 미송신 항목을 교체하므로 `true`는 원격 수신이 아니라 로컬 큐가 요청을 수락했다는 의미다.
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
bool SendUnreliablePacket(IPacket& packet);
```

세션을 통해 패킷을 전송한다.

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `packet` | `IPacket&` | 전송할 패킷 객체 |

| 반환값 | 조건 |
|--------|------|
| `true` | 로컬 송신 경로 수용. 원격 전달 성공을 뜻하지 않음 |
| `false` | 세션이 연결되지 않았거나 전송 실패 |

신뢰성 없는 전송은 위치·조준·센서 값처럼 오래된 값보다 최신 값이 중요한 데이터에만 사용한다. 로그인·인벤토리처럼 유실을 허용하지 않는 데이터에는 `SendPacket()`과 애플리케이션의 응답·중복 처리 정책을 함께 사용한다. 재전송 한도나 연결 종료가 있으므로 무조건 도착하거나 정확히 한 번 실행된다는 보장은 아니다.

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
- 신뢰성 없는 수신은 별도 최신 시퀀스 검사 뒤 같은 등록 핸들러로 전달되며 ACK를 생성하지 않는다.

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

## 송신 크기와 재전송 종료 경합

- 암호화된 프레임의 상한은 `MAX_WIRE_PACKET_SIZE = 1024`바이트다. 헤더 5 + 타입 1 + 시퀀스 8 + 패킷 ID 4 + GCM 태그 16을 포함하므로 콘텐츠 본문은 최대 990바이트이며, 문자열 길이 필드 등도 본문에 포함한다.
- reliable/unreliable 공개 송신 API는 직렬화 뒤, 시퀀스 발급과 큐 등록 전에 크기를 검사한다. 초과하면 `false`와 크기 로그를 남기고 해당 호출 자체는 세션을 종료하지 않는다. 자동 분할하지 않으므로 큰 콘텐츠는 애플리케이션이 페이지로 나눠야 한다.
- CommonCode의 NetBuffer는 실제 할당 용량을 확인해 최대 16KB까지 확장한다. 이 저장 용량은 RUDP 전송 허용 크기와 다르다. 음수·용량 초과 쓰기는 메모리 쓰기 전에 거절한다.
- `MoveWritePosThisPos`는 이미 쓴 범위 안의 임시 커서 이동만 허용한다. 공간 예약에는 `MoveWritePos`를 사용한다. `Resize`는 저장된 복귀 커서 범위도 보존하며 임시 되감기 중 재할당해도 뒤쪽 본문을 잃지 않는다.
- 서버의 여러 프레임 묶음도 UDP 데이터그램당 1024바이트 이하로 제한한다. 넘치는 다음 프레임은 기존 reserved 슬롯에 보존하고 RIO 완료 후 보낸다. 32KB 송신 버퍼는 할당 용량일 뿐 전송 크기 상한이 아니다.
- ACK와 재전송 횟수 초과는 `SendPacketInfo::completion`의 `Pending -> Acknowledged/RetryLimit` CAS로 한 번만 확정한다. ACK가 먼저 확정되면 실패 종료를 취소하고, 실패 확정 뒤의 ACK는 추적 참조만 정리한다. scheduler lock을 잡은 채 세션 종료를 호출하지 않는다. 기존 generation·진행 중 송신·참조 계수로 세션과 버퍼 수명을 보호한다.
- `retry_limit` 로그는 `wireBytes`, 완료 상태(`0=Pending, 1=Acknowledged, 2=RetryLimit`), 첫/최근 송신 경과 시간과 ACK 관측값을 포함한다. 수신 크기 거절 로그는 누적 건수 1, 2, 4, 8…에서만 출력하며 본문은 기록하지 않는다. 이 값만으로 유실 원인을 단정하지 않는다.
- 관련 회귀 테스트는 CoreTest의 NetBufferContainer/CryptoHelper/RUDPSession/RUDPIOHandler 검사에서 확인한다. 문서의 설명은 테스트 실행 결과를 대신하지 않는다.

## 비신뢰성 송신 큐 진단

```cpp
[[nodiscard]]
SessionSendContext::UnreliableQueueStats GetUnreliableQueueStats();
```

`queued`는 미송신 reserved 항목을 포함한 대기 수, `capacity`는 설정 용량, `dropped`는 세션의 로컬 교체 누계다. 반환값은 큐 mutex 아래에서 얻은 값 복사본이며 네트워크 유실률을 나타내지 않는다. 반환값을 무시하면 컴파일 경고가 발생한다. 호출 측에서 반드시 검사해야 한다.

## 관련 문서

- [[GettingStarted]] - 최소 서버 구축
- [[MultiSocketRUDPCore]] - 서버 공개 API
- [[FlowController]] - 흐름 제어 개념
- [[RetransmissionTimeoutEstimator]] - SRTT/RTTVAR 기반 RTO 계산
- [[SendPacketInfo]] - RTT 표본 유효성 추적
- [[UnreliableChannel]] - 신뢰성 없는 채널의 보장 범위와 큐 정책
