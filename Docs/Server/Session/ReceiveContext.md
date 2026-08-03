# 세션 수신 컨텍스트

> RIO receive buffer 등록부터 logic worker가 소비하는 queue까지의 소유권을 정리한다.

---

## 구성

`SessionRIOContext`는 receive와 send context를 묶는다. `SessionRecvContext`는 `RECV_OUTSTANDING_COUNT`개(현재 8개)의 `RecvBufferSlot`을 세션 수명 동안 보관한다. 각 slot은 다음 자원을 독립적으로 가진다.

- 실제 UDP 데이터그램용 16KB buffer
- 해당 slot을 가리키는 `IOContext`
- `IOContext` 내부의 원격·로컬 주소 buffer와 `RIO_BUF`
- data·원격 주소·로컬 주소 buffer의 `RIO_BUFFERID`

`RecvBuffer`는 slot 외에 사용 가능한 `IOContext*` queue와 두 개의 수명 카운터를 가진다. `outstandingRecvIo`는 RIO에 등록된 작업을, `pendingRecvLogic`은 logic queue에 인계된 작업을 추적한다. session receive request queue는 `SessionRIOContext`가 소유한다.

`RIOReceiveEx`가 데이터와 주소를 함께 채우므로 각 slot의 세 buffer를 모두 RIO에 등록한다. 등록 실패와 부분 초기화에서는 이미 등록된 buffer를 해제해야 한다.

`DoRecv()`는 free context queue를 비울 때까지 각 context를 등록한다. 동시에 등록된 receive가 같은 메모리에 쓰지 않도록 각 outstanding 작업은 고유 slot을 사용한다.

---

## 데이터 소유권 흐름

```text
SessionRecvContext의 page-locked slot buffer × 8
  → IO completion
  → 완료 IOContext의 recvDataBuffer에서 별도 NetBuffer로 복사
  → RecvIOCompletedContext가 NetBuffer와 generation을 함께 소유
  → 완료 IOContext를 free context queue에 반환
  → RecvLogic Worker가 dequeue
  → 패킷 처리 후 NetBuffer 반환
```

RIO buffer 자체를 logic worker에 넘기지 않고 복사하는 이유는 다음 receive를 즉시 재등록하면서도 완료 데이터의 수명을 분리하기 위해서다.

---

## thread-safety와 해제

- IO Worker는 완료 데이터를 생산하고 RecvLogic Worker는 queue에서 소비한다.
- receive/send `IOContext`는 RIO 등록 직전 `ownerSessionGeneration`을 저장한다. IO Worker와 RecvLogic Worker는 각 단계에서 현재 generation을 다시 검증하며 stale completion은 패킷 처리나 재등록으로 전달하지 않는다.
- IO에서 logic으로 인계할 때는 `pendingRecvLogic`을 먼저 증가시키고 queue 소유권을 확정한 다음 `outstandingRecvIo`를 감소시킨다. 이 순서는 release worker가 순간적인 0을 drain 완료로 오인하지 않게 한다.
- release thread는 소켓만 먼저 닫고 send I/O, `outstandingRecvIo`, `pendingRecvLogic`, `activeIOCompletions`가 모두 끝난 뒤 RIO buffer를 deregister한다.
- 10초 경과는 강제 완료가 아니라 지연 원인을 기록하는 경고 기준이다. 미완료 세션은 `RELEASING` 상태로 격리하며 pool에 반환하지 않는다.
- `RIORegisterBuffer`로 page-lock된 메모리는 세션 파괴 전에 반드시 deregister한다.
- `RecvIOCompletedContext`가 자신의 `NetBuffer*`를 직접 해제하므로 별도 buffer queue와 완료 marker queue의 대응 관계가 필요하지 않다.

[상세 코드](SessionComponentsReference.md#5-sessionrecvcontext-sessionriocontext-포함)

---

## 관련 문서

- [세션 컴포넌트 허브](../SessionComponents.md)
- [WorkerThreads](../Threading/WorkerThreads.md)
- [RUDPIOHandler](../RUDPIOHandler.md)
- [RIOManager](../RIOManager.md)
- [PacketProcessing](../PacketProcessing.md)
