# 치명 오류 통지와 프로세스 재시작

> 서버가 안전하게 계속 실행될 수 없는 오류의 발생 지점, 상위 레이어 통지 계약, 프로세스 재시작 요청 절차를 정리한다.

이 문서가 다루는 오류는 개별 세션 종료로 격리할 수 없는 worker·event·RIO completion queue 수준의 오류다. 코어는 프로세스를 직접 재시작하지 않는다. 코어가 최초 치명 오류를 저장하고 상위 레이어 callback을 호출하면, 상위 레이어가 제어 스레드 또는 외부 프로세스 관리자에 재시작을 요청한다.

---

## 목차

1. [핵심 계약](#핵심-계약)
2. [재시작 판단이 발생하는 지점](#재시작-판단이-발생하는-지점)
3. [오류 전달 데이터와 API](#오류-전달-데이터와-api)
4. [내부 전달 흐름](#내부-전달-흐름)
5. [상위 레이어 처리 절차](#상위-레이어-처리-절차)
6. [구현 예시](#구현-예시)
7. [정상 오류와 치명 오류의 구분](#정상-오류와-치명-오류의-구분)
8. [운영 점검표](#운영-점검표)

---

## 핵심 계약

- 치명 오류가 발생하면 오류 발생 지점에서 먼저 오류 로그를 기록한다.
- `MultiSocketRUDPCore::ReportFatalError()`는 코어 instance 수명 동안 최초 오류 하나만 저장한다.
- `SetFatalErrorHandler()`가 등록되어 있으면 오류가 발생한 worker thread에서 최초 오류를 callback으로 전달한다.
- callback이 아직 등록되지 않았다면 오류 상태는 보존된다. 이후 handler를 등록하면 저장된 오류를 즉시 전달한다.
- 상위 레이어는 callback 안에서 `StopServer()`를 동기 호출하지 않는다.
- 실제 프로세스 종료·재시작 정책은 Windows Service, container orchestrator, 별도 supervisor 같은 외부 관리 계층이 담당한다.

> **중요:** `RIO_CORRUPT_CQ`처럼 완료 정보를 더 이상 신뢰할 수 없는 상태에서는 정상 drain이 끝난다는 보장이 없다. 이때 `StopServer()`는 모든 세션의 I/O drain을 기다리다가 반환하지 못할 수 있으므로, 정상 종료 API를 재시작 수단으로 사용하지 않는다.

---

## 재시작 판단이 발생하는 지점

아래 표의 지점은 코어가 `ServerFatalError` callback을 트리거하는 위치다. 실제 restart request는 callback을 받은 상위 레이어가 제어 스레드 또는 외부 supervisor에 요청을 전달할 때 시작된다.

| 오류 코드 | 발생 함수 | 직접 조건 | 서버에 미치는 영향 | `nativeErrorCode` |
|---|---|---|---|---:|
| `RIO_COMPLETION_QUEUE_CORRUPT` | `RunIOWorkerThread()` | `RIODequeueCompletion()`이 `RIO_CORRUPT_CQ` 반환 | 해당 CQ의 완료 결과를 신뢰할 수 없어 IO Worker가 종료된다. 담당 세션의 receive/send drain이 완료되지 않을 수 있다. | `0` |
| `RECV_LOGIC_WAIT_FAILED` | `RunRecvLogicWorkerThread()` | `WaitForMultipleObjects()`가 `WAIT_FAILED` 반환 | 해당 RecvLogic Worker가 종료된다. 이미 queue에 들어간 packet과 `pendingRecvLogic`이 남을 수 있다. | `GetLastError()` |
| `RECV_LOGIC_EVENT_SIGNAL_FAILED` | `EnqueueContextResult()` | recv 완료 context enqueue 후 `SetEvent()` 실패 | queue에는 데이터가 있지만 worker wake-up을 보장할 수 없다. packet 처리와 session drain이 멈출 수 있다. | `GetLastError()` |

### `RIO_COMPLETION_QUEUE_CORRUPT`

RIO completion queue 내부 상태가 손상됐다는 의미다. 잘못된 결과 개수로 간주해 loop를 계속 돌면 `RIORESULT` 배열 범위를 벗어나거나 유효하지 않은 request context를 처리할 수 있으므로 해당 worker는 즉시 종료한다.

일반적인 원인 후보는 다음과 같다.

- completion queue 용량 또는 RIO 사용 계약 위반
- 등록된 buffer·request queue·socket의 잘못된 수명 관리
- 메모리 손상
- RIO API 호출 인자 또는 동시성 불변식 위반

코어는 이 상태를 복구 가능한 일시 오류로 보지 않는다. 반복 dequeue나 sleep 후 재시도도 수행하지 않는다.

### `RECV_LOGIC_WAIT_FAILED`

RecvLogic Worker가 packet event와 stop event를 기다리는 Windows wait API 자체가 실패한 상태다. 정상적인 timeout과 다르며, 현재 wait에는 `INFINITE`가 사용되므로 `WAIT_TIMEOUT`도 정상 경로에 존재하지 않는다.

일반적인 원인 후보는 다음과 같다.

- event handle이 잘못 닫혔거나 유효하지 않음
- handle 수명과 worker 수명의 순서 위반
- handle 값 또는 관련 메모리 손상
- 운영체제 자원 오류

### `RECV_LOGIC_EVENT_SIGNAL_FAILED`

IO Worker가 수신 완료 데이터를 logic queue에 넣은 뒤 AutoResetEvent를 signal하지 못한 상태다. enqueue는 이미 완료됐으므로 단순히 caller에 `false`를 반환해 buffer를 해제하면 queue가 가리키는 buffer와 충돌할 수 있다. 따라서 현재 코드는 queue 소유권을 유지하고 치명 오류를 통지한다.

일반적인 원인 후보는 다음과 같다.

- recv logic event handle이 이미 닫힘
- event 생성·정리 순서 위반
- 잘못된 `threadId` 또는 handle table 손상
- 운영체제 자원 오류

---

## 오류 전달 데이터와 API

### 오류 코드

```cpp
enum class SERVER_FATAL_ERROR_CODE : unsigned char
{
    RIO_COMPLETION_QUEUE_CORRUPT,
    RECV_LOGIC_WAIT_FAILED,
    RECV_LOGIC_EVENT_SIGNAL_FAILED,
};
```

### 오류 정보

```cpp
struct ServerFatalError
{
    SERVER_FATAL_ERROR_CODE code{};
    ThreadIdType threadId{};
    unsigned long nativeErrorCode{};
};
```

| 필드 | 설명 |
|---|---|
| `code` | 상위 레이어가 운영 정책을 선택할 때 사용하는 치명 오류 분류 |
| `threadId` | 문제가 발생한 IO 또는 RecvLogic worker index |
| `nativeErrorCode` | Windows API가 제공한 오류 코드. 별도 native code가 없는 CQ 손상은 `0` |

### `SetFatalErrorHandler`

```cpp
void SetFatalErrorHandler(ServerFatalErrorHandler inHandler);
```

상위 레이어 callback을 등록한다. callback은 오류 발생 worker thread에서 호출되므로 짧고 non-blocking이어야 한다. callback 내부에서는 오류 정보를 복사하고 제어 스레드를 깨우는 작업만 수행한다.

callback이 예외를 던져도 예외는 코어 밖으로 전파되지 않는다. 코어는 예외 내용을 로그로 기록하지만, 이미 확정된 치명 오류 상태는 유지한다.

### `GetFatalError`

```cpp
[[nodiscard]]
std::optional<ServerFatalError> GetFatalError() const;
```

저장된 최초 치명 오류를 조회한다. 오류가 없으면 `std::nullopt`를 반환한다. callback을 사용할 수 없는 환경에서는 제어 스레드가 이 API를 polling할 수 있지만, 운영 서버는 callback을 서버 시작 전에 등록하는 방식을 권장한다.

handler와 저장 오류에 대한 접근은 `fatalErrorLock`으로 직렬화된다. callback 호출은 lock을 해제한 뒤 수행하므로 callback이 다시 `GetFatalError()`를 호출해도 같은 mutex를 재획득하느라 교착되지 않는다.

---

## 내부 전달 흐름

```text
RIO 또는 Windows event 오류 발생
  → 오류 발생 지점에서 상세 로그 기록
  → ReportFatalError(error)
      → fatalErrorLock 획득
      → 최초 오류가 이미 있으면 추가 callback 생략
      → 최초 오류 저장
      → 등록된 handler 복사
      → fatalErrorLock 해제
  → handler(error) 호출
      → 상위 레이어의 restart-request queue 또는 control event signal
  → 제어 스레드가 health 상태를 unhealthy로 전환
  → 외부 supervisor에 process restart 요청
```

여러 worker가 거의 동시에 실패해도 `ReportFatalError()`가 확정하는 것은 최초 오류 하나다. 후속 오류는 각 발생 지점의 로그에서 확인한다. 최초 오류를 보존하는 이유는 재시작 원인 판단이 후속 연쇄 오류로 덮이지 않게 하기 위해서다. 오류 발생 후 handler를 새로 등록하거나 교체하면 저장된 최초 오류가 새 handler에 다시 전달될 수 있다.

---

## 상위 레이어 처리 절차

### 1. 서버 시작 전에 handler 등록

handler가 없는 상태에서도 코어는 오류를 저장하고 로그를 남기지만, 즉시 운영 계층에 통지하려면 `StartServer()` 전에 `SetFatalErrorHandler()`를 호출한다.

### 2. callback에서는 전달만 수행

callback은 worker thread에서 실행된다. 다음 작업만 수행한다.

- `ServerFatalError` 복사
- atomic flag 설정 또는 thread-safe queue enqueue
- 제어 스레드용 event·condition variable signal

다음 작업은 callback에서 수행하지 않는다.

- `StopServer()` 동기 호출
- worker join 대기
- 긴 파일 I/O 또는 network I/O
- callback을 발생시킨 `MultiSocketRUDPCore` 파괴
- 동일 프로세스 안에서 새 `MultiSocketRUDPCore` 즉시 생성

### 3. 제어 스레드에서 장애 상태 전환

제어 스레드는 callback이 남긴 재시작 요청을 받아 다음 순서로 처리한다.

1. readiness 또는 서비스 health를 unhealthy로 변경한다.
2. 신규 traffic 유입을 중단한다.
3. 오류 코드, worker id, native error code, 직전 로그를 보존한다.
4. metric과 alert를 전송한다.
5. 외부 supervisor에 현재 프로세스 교체를 요청한다.

### 4. 외부 프로세스 관리자가 재시작

권장되는 재시작 주체는 현재 코어와 주소 공간을 공유하지 않는 관리 계층이다.

- Windows Service Control Manager와 recovery action
- container restart policy
- Kubernetes liveness failure와 pod replacement
- 별도 watchdog 또는 process supervisor

CQ 손상이나 event 불변식 붕괴 이후에는 동일 프로세스에서 코어 객체만 재생성하는 방식보다 프로세스 전체 교체를 우선한다. 메모리 손상이나 handle table 손상 가능성을 현재 주소 공간에서 배제할 수 없기 때문이다.

---

## 구현 예시

아래 예시는 callback과 실제 재시작 정책을 분리한다. `restartController`는 프로젝트 상위 레이어가 제공하는 thread-safe 전달 객체를 뜻하는 의사 코드다.

```cpp
MultiSocketRUDPCore core(L"MY", L"DevServerCert");

core.SetFatalErrorHandler(
    [&restartController](const ServerFatalError& error) noexcept
    {
        // worker thread: 복사와 signal만 수행한다.
        restartController.RequestRestart(error);
    });

if (not core.StartServer(
        L"ServerOptionFile/CoreOption.txt",
        L"ServerOptionFile/SessionBrokerOption.txt",
        [](MultiSocketRUDPCore& inCore) -> RUDPSession*
        {
            return new Player(inCore);
        }))
{
    // 시작 실패는 StartServer() 반환값으로 처리한다.
}
```

제어 스레드의 처리는 다음과 같이 분리한다.

```cpp
void ServerController::OnRestartRequested(const ServerFatalError& error)
{
    healthReporter.MarkUnhealthy(error);
    alertReporter.ReportFatalError(error);
    diagnosticStore.FlushFatalContext(error);

    // 구현체는 Windows Service, container, watchdog 등 외부 관리 계층에 요청한다.
    processSupervisor.RequestProcessRestart();
}
```

`processSupervisor`, `healthReporter`, `alertReporter`는 MultiSocketRUDP가 제공하는 타입이 아니다. 실제 배포 환경의 제어 계층으로 대체한다.

---

## 정상 오류와 치명 오류의 구분

다음 오류는 기본적으로 프로세스 재시작 대상이 아니다.

| 상황 | 현재 처리 | 재시작 여부 |
|---|---|---|
| 개별 RIO completion의 `WSAECONNRESET` | 해당 session 정상 disconnect 사유로 처리 | 아니요 |
| session 해제 중 `WSA_OPERATION_ABORTED` | 예상된 취소 completion으로 정리 | 아니요 |
| 그 외 개별 recv/send completion 오류 | 해당 session을 `BY_ERROR`로 disconnect | 일반적으로 아니요 |
| 잘못된 packet, 복호화 실패, sequence 오류 | packet 거부 또는 session 오류 처리 | 일반적으로 아니요 |
| `StartServer()` 초기화 실패 | `false` 반환과 오류 로그 | 시작 정책에 따라 재시도 가능 |

치명 오류 callback은 개별 client나 session 문제가 아니라 공유 worker pipeline을 더 이상 신뢰할 수 없을 때 사용한다.

---

## 운영 점검표

- `SetFatalErrorHandler()`를 `StartServer()` 전에 등록했는가
- callback이 `StopServer()`나 worker join을 직접 호출하지 않는가
- callback이 thread-safe queue 또는 event를 통해 제어 스레드로 전달하는가
- `code`, `threadId`, `nativeErrorCode`가 alert와 로그에 함께 남는가
- readiness 차단과 신규 traffic 제거 절차가 있는가
- 외부 supervisor의 restart policy가 실제로 활성화되어 있는가
- restart loop를 막기 위한 횟수 제한과 backoff가 있는가
- CQ 크기, event handle 생성·정리 순서, 최근 메모리 손상 징후를 재시작 후 조사하는가

---

## 관련 문서

- [MultiSocketRUDPCore](MultiSocketRUDPCore.md) — callback 등록 API와 서버 생명주기
- [Worker thread 역할](Threading/WorkerThreads.md) — 오류가 발생하는 worker별 책임
- [RIOManager](RIOManager.md) — completion queue 생성과 `RIO_CORRUPT_CQ`
- [시작·종료와 공유 상태](Threading/LifecycleAndSynchronization.md) — 정상 drain과 종료 순서
- [문제 해결](../Troubleshooting.md) — 운영 중 로그를 기준으로 한 조사 순서
