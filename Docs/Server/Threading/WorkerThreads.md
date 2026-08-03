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
- 처리: 요청 당시 generation과 세션 유효성을 확인하고, 성공·오류·취소 completion을 모두 `RUDPIOHandler::IOCompleted`로 전달
- 출력: 수신 context enqueue, 다음 receive 등록, send mode 해제와 후속 send
- 주의: completion queue가 비어 있으면 polling이 계속된다. 현재 빌드는 compile-time 설정에 따라 항상 `Sleep(0)`을 사용하므로 `WORKER_THREAD_ONE_FRAME_MS`는 반영되지 않는다.
- 치명 오류: `RIODequeueCompletion()`이 `RIO_CORRUPT_CQ`를 반환하면 해당 worker는 오류를 상위 레이어에 전달하고 종료한다. CQ 완료를 더 이상 신뢰할 수 없으므로 프로세스 재시작이 필요하다.

[상세 코드 해설](ThreadModelReference.md#2-io-worker-thread-상세)

## RecvLogic Worker

- 입력: IO Worker가 enqueue한 수신 완료 context와 worker별 semaphore 신호
- 처리: 세션 처리 상태 표시, 패킷 사전 검증, type 분기, 복호화, 순서 보장
- 출력: 콘텐츠 handler 호출, ACK 송신, `SendPacketInfo` 정리
- 주의: 완료 context가 보관한 generation을 실행 직전에 다시 확인한다. stale이거나 `RELEASING`인 작업은 자신이 소유한 `NetBuffer`만 해제하고 콘텐츠 처리로 전달하지 않는다.
- 치명 오류: `WaitForMultipleObjects()`의 `WAIT_FAILED` 또는 IO Worker의 recv logic event `SetEvent()` 실패는 queue drain을 보장할 수 없으므로 상위 레이어 재시작 요청 대상으로 전달한다.

[상세 코드 해설](ThreadModelReference.md#3-recvlogic-worker-thread-상세)

## Retransmission Worker

- 입력: session thread id에 대응하는 scheduler의 deadline heap
- 처리: erased flag와 schedule version으로 stale entry를 걸러낸 뒤 재전송
- 출력: 새 deadline 등록 또는 재전송 한계 disconnect
- 주의: map에서 제거됐다는 사실만으로 heap entry의 수명이 끝나는 것은 아니다. 참조 카운트 해제 시점을 함께 확인한다.

[상세 코드 해설](ThreadModelReference.md#4-retransmission-thread-상세)

## Session Release Worker

- 입력: `DoDisconnect`가 만든 release 대상과 event 신호
- 처리: 소켓만 먼저 닫은 뒤 send I/O, outstanding receive, 대기 중인 receive logic을 drain하고 RIO buffer와 콘텐츠 상태 정리
- 출력: session 초기화와 unused pool 반환
- 주의: 10초 대기는 강제 해제 기준이 아니라 진단 로그 기준이다. drain되지 않은 세션은 pool에 반환하지 않는다.

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
- [치명 오류 통지와 프로세스 재시작](../FatalErrorHandling.md)
