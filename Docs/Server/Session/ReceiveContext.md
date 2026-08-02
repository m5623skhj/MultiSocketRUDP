# 세션 수신 컨텍스트

> RIO receive buffer 등록부터 logic worker가 소비하는 queue까지의 소유권을 정리한다.

---

## 구성

`SessionRIOContext`는 receive와 send context를 묶는다. `SessionRecvContext`는 `RECV_OUTSTANDING_COUNT`개(현재 8개)의 `RecvBufferSlot`을 세션 수명 동안 보관한다. 각 slot은 다음 자원을 독립적으로 가진다.

- 실제 UDP 데이터그램용 16KB buffer
- 해당 slot을 가리키는 `IOContext`
- `IOContext` 내부의 원격·로컬 주소 buffer와 `RIO_BUF`
- data·원격 주소·로컬 주소 buffer의 `RIO_BUFFERID`

`RecvBuffer`는 slot 외에 사용 가능한 `IOContext*` queue와 완료 후 복사된 `NetBuffer*` queue를 가진다. session receive request queue는 `SessionRIOContext`가 소유한다.

`RIOReceiveEx`가 데이터와 주소를 함께 채우므로 각 slot의 세 buffer를 모두 RIO에 등록한다. 등록 실패와 부분 초기화에서는 이미 등록된 buffer를 해제해야 한다.

`DoRecv()`는 free context queue를 비울 때까지 각 context를 등록한다. 동시에 등록된 receive가 같은 메모리에 쓰지 않도록 각 outstanding 작업은 고유 slot을 사용한다.

---

## 데이터 소유권 흐름

```text
SessionRecvContext의 page-locked slot buffer × 8
  → IO completion
  → 완료 IOContext의 recvDataBuffer에서 별도 NetBuffer로 복사
  → 완료 IOContext를 free context queue에 반환
  → receive queue enqueue
  → RecvLogic Worker가 dequeue
  → 패킷 처리 후 NetBuffer 반환
```

RIO buffer 자체를 logic worker에 넘기지 않고 복사하는 이유는 다음 receive를 즉시 재등록하면서도 완료 데이터의 수명을 분리하기 위해서다.

---

## thread-safety와 해제

- IO Worker는 완료 데이터를 생산하고 RecvLogic Worker는 queue에서 소비한다.
- 완료 context는 `ownerSessionId`와 session 포인터를 보관하지만 현재 generation을 저장·검증하지 않는다. session id 재사용과 stale completion이 겹치지 않는다는 추가 보장은 없다.
- release thread는 send I/O mode와 receive logic 처리 중 플래그만 확인한다. outstanding receive 개수를 세거나 모두 drain했음을 확인하지 않는다.
- release 대기가 10초를 넘으면 send mode를 강제로 `IO_NONE_SENDING`으로 바꾸고 정리를 진행한다. 이 경로는 완전한 I/O drain 보장이 아니라 best-effort 종료다.
- `RIORegisterBuffer`로 page-lock된 메모리는 세션 파괴 전에 반드시 deregister한다.
- queue에 남은 `NetBuffer*`는 cleanup에서 누락 없이 반환한다.

[상세 코드](SessionComponentsReference.md#5-sessionrecvcontext-sessionriocontext-포함)

---

## 관련 문서

- [세션 컴포넌트 허브](../SessionComponents.md)
- [WorkerThreads](../Threading/WorkerThreads.md)
- [RUDPIOHandler](../RUDPIOHandler.md)
- [RIOManager](../RIOManager.md)
- [PacketProcessing](../PacketProcessing.md)
