# 스레드 모델 허브

> 서버의 worker 역할, 시작·종료 순서, 공유 상태를 목적별로 찾는 진입점이다.

---

## 빠른 선택

| 질문 | 문서 |
|---|---|
| 각 worker가 무엇을 실행하는가 | [WorkerThreads](Threading/WorkerThreads.md) |
| 시작·종료 순서가 왜 중요한가 | [LifecycleAndSynchronization](Threading/LifecycleAndSynchronization.md) |
| 어떤 데이터가 thread 사이를 이동하는가 | [LifecycleAndSynchronization](Threading/LifecycleAndSynchronization.md#공유-데이터-흐름) |
| 실제 함수와 코드 흐름을 모두 추적하고 싶다 | [상세 레퍼런스](Threading/ThreadModelReference.md) |

---

## 전체 그룹

| 그룹 | 수 | 주 역할 | 주요 대기·종료 수단 |
|---|---:|---|---|
| IO Worker | N | RIO 완료 큐 처리 | polling, `stop_token` |
| RecvLogic Worker | N | 패킷 검증·분기·콘텐츠 전달 | worker별 semaphore, `stop_token` |
| Retransmission | N | deadline 기반 미ACK 재전송 | scheduler timer, `stop_token` |
| Session Release | 1 | `RELEASING` 세션의 안전한 반환 | release event, `stop_token` |
| Heartbeat | 1 | heartbeat와 예약 timeout | 주기 확인, `stop_token` |
| SessionBroker | 1 + worker | TLS 세션 발급 | accept 중단과 stop 요청 |
| Ticker | 1 | `TimerEvent` 실행 | 내부 stop 신호 |
| Logger | 1 | 비동기 로그 기록 | event와 stop 신호 |

N은 서버 옵션의 `THREAD_COUNT`다.

---

## 반드시 함께 확인할 동시성 계약

- 현재 IO 완료 context는 `ownerSessionId`만 보관하고 generation을 저장·검증하지 않는다. 따라서 session id 재사용과 겹치는 stale completion을 generation으로 차단한다는 보장은 없다.
- RecvLogic 처리 중인 세션은 release thread가 즉시 반환하지 않는다.
- `SendPacketInfo`는 map, retransmission scheduler, I/O 완료가 참조할 수 있으므로 ref-count와 erased/version 검사가 함께 유지돼야 한다.
- 종료 시 새 연결과 session socket을 먼저 닫은 뒤 worker에 stop을 요청한다. 현재 구현에는 모든 outstanding receive completion을 확인하는 drain barrier가 없으므로 종료 정리는 best-effort다.
- lock이 컨테이너 접근을 보호하더라도 컨테이너가 가리키는 객체 수명까지 자동으로 보장하지는 않는다.

스레드 수, sleep, timeout을 변경하면 처리량뿐 아니라 종료 지연, starvation, stale context 가능성을 함께 검토한다.

---

## 관련 문서

- [MultiSocketRUDPCore](MultiSocketRUDPCore.md)
- [PacketProcessing](PacketProcessing.md)
- [SessionLifecycle](SessionLifecycle.md)
- [RUDPIOHandler](RUDPIOHandler.md)
- [SendPacketInfo](SendPacketInfo.md)
