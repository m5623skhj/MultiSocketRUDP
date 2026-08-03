# 세션 컴포넌트 허브

> `RUDPSession`이 위임하는 상태, 자원, 수신, 송신 책임을 목적별로 찾는 진입점이다.

---

## 빠른 선택

| 질문 | 문서 |
|---|---|
| 상태 전이, 암호 키, socket 수명을 확인한다 | [StateCryptoAndSocket](Session/StateCryptoAndSocket.md) |
| RIO receive buffer와 수신 queue를 확인한다 | [ReceiveContext](Session/ReceiveContext.md) |
| 송신 queue, ACK map, pending queue, CWND를 확인한다 | [SendAndFlow](Session/SendAndFlow.md) |
| 모든 멤버와 코드 예시를 한 번에 추적한다 | [상세 레퍼런스](Session/SessionComponentsReference.md) |

---

## 책임 경계

```text
RUDPSession
  ├─ SessionStateMachine   상태 전이
  ├─ SessionCryptoContext  AES-GCM key/salt/handle
  ├─ SessionSocketContext  socket과 close 동기화
  ├─ SessionRIOContext
  │    ├─ SessionRecvContext  RIO receive buffer와 완료 queue
  │    └─ SessionSendContext  batch send, ACK 추적, pending queue
  ├─ RUDPFlowManager       CWND와 receive window
  └─ SessionPacketOrderer  수신 순서 보장
```

각 컴포넌트의 lock과 atomic은 자신의 상태만 보호한다. 세션 전체의 안전성은 상태 전이, generation 검증, I/O drain, release 순서를 함께 검토해야 성립한다. receive/send `IOContext`는 등록 당시 generation을 저장하고, IO Worker와 RecvLogic Worker가 이를 재검증한다.

---

## 수명 체크포인트

- 예약 시 state, crypto, socket, RIO가 사용 가능한 순서로 초기화돼야 한다.
- 연결 성공 전 받은 패킷은 session state와 원격 주소 검증을 통과해야 한다.
- release는 소켓만 먼저 닫고 send I/O, outstanding receive, 대기 중인 receive logic을 모두 drain한 뒤 RIO buffer와 세션을 정리한다. 10초 제한은 강제 진행이 아니라 격리 상태의 경고 기준이다.
- `SessionCryptoContext`의 key handle이 살아 있는 동안 key object buffer도 유효해야 한다.
- `SessionSendContext`의 map에서 지운 항목이 retransmission heap에 남아 있을 수 있으므로 erased/version/ref-count 계약을 유지한다.

---

## 관련 문서

- [RUDPSession](RUDPSession.md)
- [SessionLifecycle](SessionLifecycle.md)
- [ThreadModel](ThreadModel.md)
- [RUDPIOHandler](RUDPIOHandler.md)
- [PacketProcessing](PacketProcessing.md)
