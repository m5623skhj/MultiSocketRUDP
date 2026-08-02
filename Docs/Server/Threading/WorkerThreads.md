# Worker thread 역할

> 서버 worker group별 입력, 처리, 출력과 실패 영향을 빠르게 대조한다.

---

## 목차

1. [실행 흐름 요약](#실행-흐름-요약)
2. [IO Worker](#io-worker)
3. [RecvLogic Worker](#recvlogic-worker)
4. [Retransmission Worker](#retransmission-worker)
5. [Session Release Worker](#session-release-worker)
6. [Heartbeat Worker](#heartbeat-worker)

---

## 실행 흐름 요약

```text
RIO 완료
  → IO Worker
  → 수신 context queue + worker semaphore
  → RecvLogic Worker
  → 패킷 검증·복호화·순서 보장
  → 콘텐츠 handler

송신 등록
  → RIO send 완료 또는 ACK
  → SendPacketInfo 정리 / pending flush

ACK timeout
  → Retransmission Worker
  → 재전송 또는 disconnect 요청
  → Session Release Worker
  → 자원 정리와 pool 반환
```

---

## IO Worker

- 입력: thread별 RIO completion queue의 `RIORESULT`
- 처리: context와 세션 유효성 확인 후 `RUDPIOHandler::IOCompleted` 호출
- 출력: 수신 context enqueue, 다음 receive 등록, send mode 해제와 후속 send
- 주의: completion queue가 비어 있으면 polling이 계속된다. 현재 빌드는 compile-time 설정에 따라 항상 `Sleep(0)`을 사용하므로 `WORKER_THREAD_ONE_FRAME_MS`는 반영되지 않는다.

[상세 코드 해설](ThreadModelReference.md#2-io-worker-thread-상세)

## RecvLogic Worker

- 입력: IO Worker가 enqueue한 수신 완료 context와 worker별 semaphore 신호
- 처리: 세션 처리 상태 표시, 패킷 사전 검증, type 분기, 복호화, 순서 보장
- 출력: 콘텐츠 handler 호출, ACK 송신, `SendPacketInfo` 정리
- 주의: 처리 중 플래그는 release worker가 확인하지만 현재 receive `IOContext`에는 generation 검증이 없다. session id 재사용과 stale completion 가능성을 별도로 검토한다.

[상세 코드 해설](ThreadModelReference.md#3-recvlogic-worker-thread-상세)

## Retransmission Worker

- 입력: session thread id에 대응하는 scheduler의 deadline heap
- 처리: erased flag와 schedule version으로 stale entry를 걸러낸 뒤 재전송
- 출력: 새 deadline 등록 또는 재전송 한계 disconnect
- 주의: map에서 제거됐다는 사실만으로 heap entry의 수명이 끝나는 것은 아니다. 참조 카운트 해제 시점을 함께 확인한다.

[상세 코드 해설](ThreadModelReference.md#4-retransmission-thread-상세)

## Session Release Worker

- 입력: `DoDisconnect`가 만든 release 대상과 event 신호
- 처리: send I/O mode와 receive logic 처리 플래그를 확인한 뒤 소켓, 패킷 정보, 콘텐츠 상태 정리
- 출력: session 초기화와 unused pool 반환
- 주의: outstanding receive 전체를 drain하지 않으며, 대기가 10초를 넘으면 send mode를 강제로 해제하고 정리하는 best-effort 경로다.

[상세 코드 해설](ThreadModelReference.md#5-session-release-thread-상세)

## Heartbeat Worker

- 입력: 주기 tick과 사용 중·예약 중 session 상태
- 처리: heartbeat 송신, 예약 timeout 검사
- 출력: 재전송 추적 항목 또는 예약 취소·release
- 주의: heartbeat, alive check, retransmission timeout을 독립적으로 조정하면 서로 다른 계층이 같은 연결을 중복 종료할 수 있으므로 시간 관계를 함께 검토한다.

[상세 코드 해설](ThreadModelReference.md#6-heartbeat-thread-상세)

---

## 관련 문서

- [스레드 모델 허브](../ThreadModel.md)
- [시작·종료와 공유 상태](LifecycleAndSynchronization.md)
- [RUDPIOHandler](../RUDPIOHandler.md)
- [PacketProcessing](../PacketProcessing.md)
