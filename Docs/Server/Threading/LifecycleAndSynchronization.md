# 스레드 시작·종료와 공유 상태

> worker 간 선행 조건, 종료 순서, 객체 수명과 공유 데이터 계약을 정리한다.

---

## 목차

1. [시작 순서](#시작-순서)
2. [종료 순서](#종료-순서)
3. [공유 데이터 흐름](#공유-데이터-흐름)
4. [변경 리뷰 체크리스트](#변경-리뷰-체크리스트)

---

## 시작 순서

```text
Ticker 시작
  → worker 대기 handle 준비
  → Session Release / Heartbeat 시작
  → IO Worker 시작
  → RecvLogic Worker 시작
  → Retransmission Worker 시작
  → 준비 대기
  → SessionBroker 시작
```

핵심 원칙은 외부 연결을 받기 전에 수신, logic, 재전송, release 기반 시설을 모두 준비하는 것이다. broker를 너무 일찍 시작하면 최초 데이터그램이 도착했을 때 receive 등록이나 consumer가 아직 준비되지 않을 수 있다.

[상세 시작 순서](ThreadModelReference.md#7-스레드-시작-순서와-이유)

---

## 종료 순서

```text
SessionBroker 중단
  → 새 연결 차단
  → 모든 session을 RELEASING으로 전환
  → 모든 session socket만 닫기
  → IO/RecvLogic/Release worker를 유지해 completion drain
  → RIO buffer deregister와 session pool 반환
  → logic / release / retransmission stop event 설정
  → worker stop 요청과 join
  → timer·logger 등 후속 자원 정리
```

`StopServer()`는 모든 세션이 unused pool로 돌아올 때까지 IO, RecvLogic, Session Release worker를 유지한다. 각 세션은 send I/O와 `outstandingRecvIo`, `pendingRecvLogic`, `activeIOCompletions`가 모두 끝난 뒤에만 RIO buffer를 해제하고 generation을 증가시킨다.

종료 순서를 바꿀 때는 다음 실패 모드를 먼저 검토한다.

- IO Worker보다 consumer를 먼저 멈추면 완료 신호와 queue가 남는다.
- release worker보다 session 자원을 먼저 파괴하면 완료 context가 해제된 객체를 참조할 수 있다.
- 재전송 heap과 send map을 함께 정리하지 않으면 stale entry의 중복 해제가 가능하다.
- logger를 너무 일찍 멈추면 종료 경로의 원인 로그가 사라진다.

[상세 종료 순서](ThreadModelReference.md#8-스레드-종료-순서와-이유)

---

## 공유 데이터 흐름

| 생산자 | 공유 지점 | 소비자 | 보호·유효성 조건 |
|---|---|---|---|
| IO Worker | `RecvIOCompletedContext` | RecvLogic Worker | worker AutoResetEvent, generation, `pendingRecvLogic` |
| 콘텐츠·송신 경로 | send map, pending queue | IO/ACK 처리 | send mode, map lock/ownership 계약 |
| 송신 경로 | retransmission scheduler | Retransmission Worker | schedule version, erased flag, ref-count |
| 모든 disconnect 경로 | release target queue | Session Release Worker | session state, 처리 중·I/O 상태 |
| Heartbeat Worker | heartbeat send info | Retransmission Worker | session thread id, session lifetime |

[전체 데이터 흐름](ThreadModelReference.md#9-스레드-간-공유-데이터-흐름)

---

## 변경 리뷰 체크리스트

- 이 함수는 어느 thread에서 호출되는가?
- 같은 객체를 다른 thread가 동시에 읽거나 쓰는가?
- lock이나 atomic이 보호하는 범위는 값, 컨테이너, 객체 수명 중 어디까지인가?
- wait 중인 thread를 종료 시 반드시 깨우는가?
- context가 session id 재사용 뒤에도 도착할 수 있는가? RIO 등록 시점과 logic 실행 시점의 generation 검증이 모두 유지되는가?
- 종료 경로가 `outstandingRecvIo`, `pendingRecvLogic`, send I/O를 실제로 drain하는가?
- ref-count를 증가한 모든 경로에 정확히 한 번의 `Free`가 있는가?
- partial initialization 실패에서도 이미 시작한 thread가 join되는가?

---

## 관련 문서

- [스레드 모델 허브](../ThreadModel.md)
- [Worker thread 역할](WorkerThreads.md)
- [SessionLifecycle](../SessionLifecycle.md)
- [SendPacketInfo](../SendPacketInfo.md)
- [RUDPThreadManager](../RUDPThreadManager.md)
