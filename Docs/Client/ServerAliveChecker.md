# ServerAliveChecker

> **인증에 성공한 수신 횟수를 기준으로 서버 생존을 감시하는 C++ 클라이언트 모듈.**
> 신뢰성·비신뢰성 데이터, heartbeat와 ACK를 모두 활동으로 인정한다.
> 감시 스레드에서 코어 종료 콜백을 호출할 수 있으므로 self-join을 피한다.

---

## 목차

1. [동작 원리](#1-동작-원리)
2. [생성자 — 콜백 주입](#2-생성자--콜백-주입)
3. [시작 — `StartServerAliveCheck`](#3-시작--startserveralivecheck)
4. [생존 판정 — `IsServerAlive`](#4-생존-판정--isserveralive)
5. [종료 — `StopServerAliveCheck`](#5-종료--stopserveralivecheck)
6. [동시성과 객체 수명](#6-동시성과-객체-수명)
7. [타이밍 설정](#7-타이밍-설정)

---

## 1. 동작 원리

`RUDPClientCore::ProcessRecvPacket()`은 패킷 타입에 맞는 AES-GCM 인증이 성공한 직후 `authenticatedReceiveCount`를 증가시킨다. 애플리케이션의 수신 큐 소비 여부와는 무관하다.

활동으로 인정되는 패킷은 다음과 같다.

- 신뢰성 콘텐츠 `SEND_TYPE`
- 비신뢰성 콘텐츠 `UNRELIABLE_SEND_TYPE`
- `HEARTBEAT_TYPE`
- `SEND_REPLY_TYPE`

인증 실패 패킷, 잘못된 타입, 복호화 전에 거부된 패킷은 횟수를 증가시키지 않는다.

```text
StartServerAliveCheck(interval)
  → 시작 시 현재 receiveCount를 기준값으로 저장
  → interval만큼 Sleep
  → 현재 receiveCount와 기준값 비교
      ├─ 증가함: 기준값 갱신 후 계속 감시
      └─ 동일함: "Server is not alive" 기록
                  → coreStopFunction() 호출
                  → 감시 루프 종료
```

수신 시퀀스가 아니라 인증된 수신 횟수를 사용하므로 비신뢰성 전용 통신과 애플리케이션이 아직 소비하지 않은 heartbeat도 생존 상태에 반영된다.

---

## 2. 생성자 — 콜백 주입

```cpp
explicit ServerAliveChecker(
    const std::function<void()>& inCoreStopFunction,
    const std::function<uint64_t()>& inGetReceiveCountFunction);
```

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `inCoreStopFunction` | `const std::function<void()>&` | 서버 무응답 시 호출할 코어 종료 콜백 |
| `inGetReceiveCountFunction` | `const std::function<uint64_t()>&` | 인증에 성공한 누적 수신 횟수를 반환하는 콜백 |

`RUDPClientCore`는 아래 의미의 람다를 주입한다.

```cpp
RUDPClientCore::RUDPClientCore()
    : serverAliveChecker(
        [this] { Stop(); },
        [this] {
            return authenticatedReceiveCount.load(std::memory_order_relaxed);
        })
{
}
```

checker는 `RUDPClientCore`의 구체 타입이나 수신 큐 구조를 알지 않는다. 단, 두 콜백이 `this`를 캡처하므로 checker 스레드가 끝나기 전에 owner를 파괴하면 안 된다.

---

## 3. 시작 — `StartServerAliveCheck`

```cpp
void StartServerAliveCheck(unsigned int inCheckIntervalMs);
```

검사 주기를 저장하고, 현재 수신 횟수를 `beforeCheckReceiveCount`의 기준값으로 설정한 뒤 감시 `std::jthread`를 시작한다.

```cpp
checkIntervalMs = inCheckIntervalMs;
beforeCheckReceiveCount = getReceiveCountFunction();
isStopped.store(false, std::memory_order_release);
serverAliveCheckThread = std::jthread(
    &ServerAliveChecker::RunServerAliveCheckerThread, this);
```

`RUDPClientCore::OnSendReply()`이 CONNECT의 `LOGIN_PACKET_SEQUENCE` ACK를 처리해 연결 상태로 전환한 뒤 호출한다. 시작 시 현재 횟수를 기준값으로 다시 읽으므로 이전 연결의 검사값을 이어받지 않는다.

> `StartServerAliveCheck()`는 동시 호출이나 이미 실행 중인 checker의 재시작을 위한 API가 아니다. 연결 수명주기에서 한 번만 직렬 호출해야 한다.

---

## 4. 생존 판정 — `IsServerAlive`

```cpp
[[nodiscard]]
bool IsServerAlive(uint64_t receiveCount);
```

| 반환값 | 조건 |
|--------|------|
| `true` | 전달된 누적 수신 횟수가 직전 기준값과 다름. 기준값도 새 값으로 갱신한다. |
| `false` | 전달된 누적 수신 횟수가 직전 기준값과 동일함. |

> 반환값을 무시하면 컴파일 경고가 발생한다. 호출 측에서 반드시 검사해야 한다.

현재 누적 횟수는 증가만 하므로 구현의 `!=` 비교는 검사 구간에 인증된 수신이 하나 이상 있었는지를 뜻한다.

---

## 5. 종료 — `StopServerAliveCheck`

```cpp
void StopServerAliveCheck();
```

`isStopped.exchange(true, std::memory_order_acq_rel)`로 중복 종료 요청을 막는다. 스레드가 join 가능한 경우 호출 스레드에 따라 다음처럼 처리한다.

```text
외부 스레드에서 호출
  → serverAliveCheckThread.join()

감시 스레드 자신에서 호출
  → serverAliveCheckThread.detach()
  → coreStopFunction()이 반환되면 감시 루프도 종료
```

checker 루프는 `std::jthread`의 `stop_token`이 아니라 `isStopped`를 검사한다. 또한 `Sleep(checkIntervalMs)` 중에는 종료를 확인하지 않으므로 외부 스레드의 `join()`은 최대 검사 주기만큼 대기할 수 있다.

---

## 6. 동시성과 객체 수명

- `isStopped`는 atomic이며 시작은 release store, 루프와 종료는 acquire/acq_rel 연산을 사용한다.
- `authenticatedReceiveCount`는 활동 존재 여부만 필요하므로 relaxed atomic으로 증가·조회한다.
- `beforeCheckReceiveCount`와 `checkIntervalMs`는 감시 시작 전에 설정하고 이후 checker 스레드만 읽거나 갱신한다.
- self-join 경로는 교착을 피하기 위해 detach한다. 이 경로에서 `Stop()` 반환만으로 checker 함수가 완전히 빠져나왔다고 보장할 수는 없다.
- owner는 checker 스레드가 콜백과 루프를 모두 끝낼 때까지 `ServerAliveChecker`와 캡처 대상의 수명을 유지해야 한다.

---

## 7. 타이밍 설정

현재 샘플 설정은 다음과 같다.

```ini
; 서버 CoreOption.txt
HEARTBEAT_THREAD_SLEEP_MS = 5000

; C++ 클라이언트 CoreOption.txt
SERVER_ALIVE_CHECK_MS = 15000
```

`SERVER_ALIVE_CHECK_MS`는 정상 heartbeat 주기보다 길어야 한다. 네트워크 지연과 worker 스케줄링 변동을 고려해 heartbeat 주기의 두 배 이상을 시작점으로 삼고, 실제 종료 감지 요구 시간과 함께 조정한다.

---

## 관련 문서

- [[RUDPClientCore]] — 수신 횟수 증가와 checker 시작·종료 위치
- [[UnreliableChannel]] — 비신뢰성 수신도 생존 활동으로 인정하는 규칙
- [[Troubleshooting]] — `Server is not alive` 로그 조사
- [[PerformanceTuning]] — heartbeat와 생존 검사 주기
