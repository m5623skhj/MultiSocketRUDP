# ServerAliveChecker

> **서버가 일정 시간 동안 응답하지 않을 경우 클라이언트를 자동 종료하는 생존 감시 모듈.**  
> 수신 시퀀스 번호의 변화를 폴링해 서버 생존 여부를 판단한다.  
> 자기 자신 스레드에서 `Stop()`이 호출될 때 발생하는 deadlock을 `detach`로 방지한다.

---

## 목차

1. [동작 원리](#1-동작-원리)
2. [생성자 — 콜백 주입](#2-생성자--콜백-주입)
3. [시작 — StartServerAliveCheck](#3-시작--startserveralivecheck)
4. [종료 — StopServerAliveCheck](#4-종료--stopserveralivecheck)
5. [내부 루프 상세](#5-내부-루프-상세)
6. [Deadlock 방지 설계](#6-deadlock-방지-설계)
7. [타이밍 설정 가이드](#7-타이밍-설정-가이드)
8. [전체 호출 시나리오](#8-전체-호출-시나리오)

---

## 1. 동작 원리

서버는 주기적으로 `HEARTBEAT_TYPE` 패킷을 전송한다.  
클라이언트 `recvThread`가 이 패킷을 수신할 때마다 `nextRecvPacketSequence`가 증가한다.

`ServerAliveChecker`는 이 시퀀스 값을 `checkIntervalMs`마다 폴링해 변화를 확인한다.

```
매 checkIntervalMs:
  nowSequence = getNextRecvSequenceFunction()

  if nowSequence == beforeCheckSequence:
    → 그 동안 패킷이 하나도 안 왔음 = 서버 무응답
    → LOG: "Server is not alive"
    → coreStopFunction()  // RUDPClientCore::Stop()
    → 루프 종료

  else:
    → 정상, 시퀀스 갱신
    → beforeCheckSequence = nowSequence
    → 계속 모니터링
```

**왜 `HEARTBEAT` 전용 카운터가 아닌 `nextRecvPacketSequence`를 사용하는가:**

`nextRecvPacketSequence`는 HEARTBEAT뿐 아니라 일반 데이터 패킷 수신 시에도 증가한다.  
즉, 서버가 데이터를 보내는 동안은 HEARTBEAT 없이도 생존으로 판단한다.  
HEARTBEAT 전용 카운터를 쓰면 데이터 트래픽이 활발한 동안 불필요한 종료가 발생할 수 있다.

---

## 2. 생성자 — 콜백 주입

```cpp
class ServerAliveChecker {
public:
    explicit ServerAliveChecker(
        const std::function<void()>& inCoreStopFunction,
        const std::function<PacketSequence()>& inGetNextRecvSequenceFunction
    );
};
```

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `inCoreStopFunction` | `std::function<void()>` | 서버 무응답 시 호출할 종료 콜백 |
| `inGetNextRecvSequenceFunction` | `std::function<PacketSequence()>` | 현재 수신 시퀀스 조회 콜백 |

**`RUDPClientCore` 생성자에서 람다로 주입:**

```cpp
RUDPClientCore::RUDPClientCore()
    : serverAliveChecker(
        // 종료 콜백: Stop() 호출
        [this] { this->Stop(); },

        // 시퀀스 조회 콜백: nextRecvPacketSequence 반환
        [this] { return this->nextRecvPacketSequence; }
    )
{}
```

**왜 직접 포인터 대신 `std::function`을 사용하는가:**

- `RUDPClientCore`의 메서드를 멤버 함수 포인터로 직접 참조하면  
  `ServerAliveChecker`가 `RUDPClientCore`에 강하게 결합된다.
- 람다 캡처 방식은 구조를 느슨하게 유지하며, 단위 테스트에서  
  Mock 함수를 쉽게 주입할 수 있다.

---

## 3. 시작 — `StartServerAliveCheck`

```cpp
void ServerAliveChecker::StartServerAliveCheck(unsigned int inCheckIntervalMs)
```

**언제 호출되는가:**

```cpp
// RUDPClientCore::OnSendReply (recvThread에서)
if (ackedSeq == LOGIN_PACKET_SEQUENCE && !isConnected) {
    isConnected = true;
    serverAliveChecker.StartServerAliveCheck(serverAliveCheckMs);
}
```

서버로부터 `sequence=0` ACK를 수신한 시점, 즉 UDP RUDP 연결이 **완전히 수립된 후**에만 시작한다.

**왜 연결 완료 후에 시작해야 하는가:**

```
연결 수립 전:
  CONNECT 패킷 전송 직후 아직 서버가 ACK를 안 보냄
  → nextRecvPacketSequence = 0으로 유지
  → 첫 checkIntervalMs 후 변화 없음 감지
  → 오탐으로 Stop() 호출

연결 수립 후 (sequence=0 ACK 수신):
  서버가 정기적으로 HEARTBEAT 전송
  → nextRecvPacketSequence가 주기적으로 증가
  → 정상 모니터링 가능
```

**내부 구현:**

```cpp
void ServerAliveChecker::StartServerAliveCheck(unsigned int inCheckIntervalMs)
{
    if (isStopped) return;  // 이미 종료됨

    checkIntervalMs = inCheckIntervalMs;

    // 초기 기준값 설정
    beforeCheckSequence = getNextRecvSequenceFunction();

    serverAliveCheckThread = std::jthread([this] {
        RunServerAliveCheckerThread();
    });
}
```

---

## 4. 종료 — `StopServerAliveCheck`

```cpp
void ServerAliveChecker::StopServerAliveCheck()
```

**`RUDPClientCore::JoinThreads()`에서 호출:**

```cpp
void JoinThreads() {
    serverAliveChecker.StopServerAliveCheck();  // ← 먼저 처리
    retransmissionThread.join();
    sendThread.join();
    recvThread.join();
}
```

**내부 구현 (Deadlock 방지 포함):**

```cpp
void ServerAliveChecker::StopServerAliveCheck()
{
    if (isStopped.exchange(true)) return;  // 이미 종료됨

    if (!serverAliveCheckThread.joinable()) return;

    // 자기 자신 스레드에서 호출되면 join() 불가 → detach
    if (serverAliveCheckThread.get_id() == std::this_thread::get_id()) {
        serverAliveCheckThread.detach();
    } else {
        // 다른 스레드에서 호출되면 안전하게 join
        serverAliveCheckThread.join();
    }
}
```

---

## 5. 내부 루프 상세

```cpp
void ServerAliveChecker::RunServerAliveCheckerThread()
{
    while (!isStopped) {
        // checkIntervalMs 동안 대기 (분할 sleep으로 stop 신호 빠르게 감지)
        const int sleepStep = 100;  // 100ms씩 나눠 sleep
        for (int elapsed = 0;
             elapsed < static_cast<int>(checkIntervalMs) && !isStopped;
             elapsed += sleepStep) {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepStep));
        }

        if (isStopped) break;

        // 시퀀스 변화 확인
        PacketSequence nowSequence = getNextRecvSequenceFunction();

        if (nowSequence == beforeCheckSequence) {
            // 변화 없음 → 서버 무응답
            LOG_ERROR(std::format(
                "Server is not alive. nextRecvSeq={}, beforeSeq={}",
                nowSequence, beforeCheckSequence));

            coreStopFunction();  // RUDPClientCore::Stop() 호출
            break;               // 루프 종료
        }

        // 정상: 기준값 갱신
        beforeCheckSequence = nowSequence;
    }
}
```

**분할 sleep의 이유:**

```
단순 sleep_for(checkIntervalMs):
  Stop() 호출 시 최대 checkIntervalMs(5000ms)까지 대기
  → 클라이언트 종료 지연

100ms씩 나눠 sleep:
  isStopped 확인 주기 = 100ms
  → Stop() 후 최대 100ms 안에 스레드 종료
```

---

## 6. Deadlock 방지 설계

### 문제 시나리오

```
[ServerAliveCheckThread]
  RunServerAliveCheckerThread()
    → 서버 무응답 감지
    → coreStopFunction()      // = RUDPClientCore::Stop()
         → JoinThreads()
              → serverAliveChecker.StopServerAliveCheck()
                   → serverAliveCheckThread.join()  ← 자기 자신을 join!
                        → DEADLOCK (영원히 대기)
```

### 해결: `get_id()` 비교 + `detach`

```cpp
if (serverAliveCheckThread.get_id() == std::this_thread::get_id()) {
    serverAliveCheckThread.detach();
    // detach: join 없이 스레드를 독립적으로 실행
    // 이 시점 직후 스레드가 자연스럽게 종료됨 (break 후 return)
} else {
    serverAliveCheckThread.join();  // 다른 스레드 → 안전한 join
}
```

**`detach` 후 스레드 상태:**

```
detach 호출 시점: coreStopFunction() 내부 (= RunServerAliveCheck 내)
  → isStopped = true (exchange에서 설정)
  → RunServerAliveCheck의 break 직전
  → 스레드가 곧 return으로 자연 종료됨
  → detach된 스레드는 백그라운드에서 종료

메모리 안전성:
  serverAliveChecker는 RUDPClientCore의 멤버 → Stop() 종료까지 유효
  detach 후 스레드가 접근하는 변수들이 모두 유효한 범위 내에 있음
```

### `isStopped.exchange(true)` 원자 연산

```cpp
if (isStopped.exchange(true)) return;  // 이미 true였으면 즉시 반환
```

`exchange`는 값을 쓰고 이전 값을 반환한다. 멀티스레드에서 중복 호출 방지.  
`StopServerAliveCheck`가 복수의 스레드에서 동시에 호출돼도 안전하다.

---

## 7. 타이밍 설정 가이드

```ini
; 서버 설정
HEARTBEAT_THREAD_SLEEP_MS=3000   ; 서버가 3초마다 HEARTBEAT 전송

; 클라이언트 설정
SERVER_ALIVE_CHECK_MS=5000       ; 5초마다 시퀀스 변화 확인
```

**최소 요구 관계:**

```
SERVER_ALIVE_CHECK_MS > HEARTBEAT_THREAD_SLEEP_MS

이유:
  서버가 3초마다 HEARTBEAT를 보냄
  클라이언트가 5초마다 확인하면 → 최소 1개의 HEARTBEAT가 수신됨
  → 정상 감지

  5초 < 3초 설정 시:
  → HEARTBEAT 오기 전에 이미 체크 → 오탐으로 Stop()
```

**권장 배율:**

```
SERVER_ALIVE_CHECK_MS >= HEARTBEAT_THREAD_SLEEP_MS × 2

예: 서버 3000ms → 클라이언트 6000ms 이상
  → 네트워크 지연 + 재전송으로 HEARTBEAT가 한 번 늦게 도착해도 오탐 없음
```

---

## 8. 전체 호출 시나리오

### 정상 시나리오 (서버 응답 중)

```
[시작]
RUDPClientCore::Start()
  → ... → OnSendReply(seq=0) → StartServerAliveCheck(5000)

[모니터링 중]
  t=5s:  nowSeq=10, before=0 → 변화 있음 → before=10
  t=10s: nowSeq=22, before=10 → 변화 있음 → before=22
  ...

[종료]
RUDPClientCore::Stop()
  → JoinThreads()
      → StopServerAliveCheck()
          → get_id() != this_thread::get_id()  ← 다른 스레드
          → isStopped=true
          → serverAliveCheckThread.join()
```

### 서버 무응답 시나리오

```
[서버 다운]
  (HEARTBEAT 전송 중단)
  nextRecvPacketSequence 증가 멈춤

  t=5s:  nowSeq=30, before=30 → 변화 없음!
    → LOG: "Server is not alive"
    → coreStopFunction()  = RUDPClientCore::Stop()
         → JoinThreads()
              → StopServerAliveCheck()
                   → get_id() == this_thread::get_id()  ← 자기 자신!
                   → isStopped=true (exchange)
                   → detach()

[RunServerAliveCheck]
  break → return → 스레드 자연 종료
```

---

## 관련 문서
- [[RUDPClientCore]] — StartServerAliveCheck 호출 시점, JoinThreads
- [[Troubleshooting]] — "Server is not alive" 로그 해석
- [[PerformanceTuning]] — HEARTBEAT 주기 설정
---

## 현재 코드 기준 함수 설명 및 정정

### 공개 함수

#### `ServerAliveChecker(const std::function<void()>& inCoreStopFunction, const std::function<PacketSequence()>& inGetNextRecvSequenceFunction)`
- 필수 콜백을 받는 생성자다.
- 코어 중지 요청과 최근 수신 시퀀스 조회를 외부에 위임한다.

#### `void StartServerAliveCheck(unsigned int inCheckIntervalMs)`
- 서버 생존 감시 스레드를 시작한다.

#### `void StopServerAliveCheck()`
- 감시 스레드 중지를 요청한다.

#### `bool IsServerAlive(PacketSequence nowPacketSequence)`
- 이전 체크 지점 대비 수신 시퀀스가 진전됐는지 확인한다.

### 내부 함수

#### `void RunServerAliveCheckerThread()`
- 주기적으로 시퀀스 진전을 검사하고, 진전이 없으면 `coreStopFunction`을 호출한다.

### 정정 메모

- 현재 헤더의 내부 스레드 함수 이름은 `RunServerAliveCheckerThread()`다.
- 문서에 남아 있던 `RunServerAliveCheck()` 표기는 현재 코드와 맞지 않는다.
