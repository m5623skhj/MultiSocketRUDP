# ServerAliveChecker

> 인증에 성공한 수신 횟수로 서버 생존을 검사하는 C++ 클라이언트 모듈이다.
> 구현은 `MultiSocketRUDP/MultiSocketRUDPClient/ServerAliveChecker.*`에 있다.

## 목차

1. [생존 판정](#생존-판정)
2. [시작과 종료](#시작과-종료)
3. [동시성과 수명](#동시성과-수명)
4. [설정](#설정)

## 생존 판정

```cpp
explicit ServerAliveChecker(
    const std::function<void()>& inCoreStopFunction,
    const std::function<uint64_t()>& inGetReceiveCountFunction);

[[nodiscard]]
bool IsServerAlive(uint64_t receiveCount);
```

`RUDPClientCore::ProcessRecvPacket()`은 신뢰성·비신뢰성 데이터, 하트비트, ACK의 인증 성공 직후 `authenticatedReceiveCount`를 증가시킨다. 인증 실패 패킷은 제외하며 애플리케이션의 수신 큐 소비 여부는 영향을 주지 않는다.

`IsServerAlive()`는 직전 기준값과 다르면 기준값을 갱신하고 `true`, 같으면 `false`를 반환한다. 반환값을 무시하면 컴파일 경고가 발생한다. 호출 측에서 반드시 검사해야 한다.

## 시작과 종료

```cpp
void StartServerAliveCheck(unsigned int inCheckIntervalMs);
void StopServerAliveCheck();
```

`StartServerAliveCheck()`는 `threadLock`을 획득한다. 기존 스레드가 join 가능한 상태면 새로 시작하지 않는다. 검사 주기와 현재 수신 횟수를 저장하고 `isStopped=false`로 설정한 뒤 `std::jthread`를 생성한다. 코어는 최초 CONNECT ACK를 수락한 뒤 이 함수를 호출한다.

```text
감시 스레드
  → condition_variable.wait_for(검사 주기, 종료 조건)
  → 종료 통지를 받으면 즉시 루프 종료
  → 수신 횟수가 진행했으면 다음 검사
  → 진행하지 않았으면 로그 기록 후 coreStopFunction() 호출
  → 루프 종료
```

코어가 주입하는 콜백은 `Stop()`이 아니라 `RequestStop()`이다. 감시 스레드는 종료를 요청하고 반환하며, 별도 `shutdownThread`가 정리를 수행한다.

`StopServerAliveCheck()`는 `threadLock`과 `waitLock`으로 종료 상태를 설정하고 `wake.notify_all()`로 대기를 깨운 뒤 감시 스레드를 join한다. 예전 `Sleep(checkIntervalMs)`·self-detach 경로는 사용하지 않는다.

## 동시성과 수명

- `threadLock`은 스레드 생성·join을 직렬화하고 `waitLock`은 조건 변수 대기와 종료 통지를 연결한다.
- 수신 횟수와 `isStopped`는 atomic이다. 기준 수신 횟수는 시작 전에 설정한 뒤 감시 스레드에서 갱신한다.
- 사용자 콜백은 감시 스레드 자신을 join하는 함수를 동기 호출하면 안 된다. 코어의 `RequestStop()` 콜백은 이 조건을 지킨다.
- 코어 정리는 수신 스레드를 먼저 join한 뒤 검사기를 종료한다. 수신 중 CONNECT ACK가 검사기를 뒤늦게 시작하는 경합을 막기 위한 순서다.
- 소유자는 감시 스레드의 join이 끝날 때까지 검사기와 콜백이 캡처한 객체를 유지해야 한다.

## 설정

샘플 서버의 `HEARTBEAT_THREAD_SLEEP_MS`는 5,000ms, C++ 클라이언트의 `SERVER_ALIVE_CHECK_MS`는 15,000ms다. 검사 주기는 정상 하트비트와 네트워크·스케줄링 지연을 수용하도록 설정한다.

## 관련 문서

- [[RUDPClientCore]] — 종료 요청과 정리 스레드
- [[UnreliableChannel]] — 인증된 수신 기반 생존 검사
- [[Troubleshooting]] — 무응답 조사
- [[PerformanceTuning]] — 검사 주기 조정
