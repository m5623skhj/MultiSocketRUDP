# 세션 송신 컨텍스트와 흐름 제어

> 신뢰·신뢰성 없는 송신 queue, ACK 추적, 재전송, pending queue와 CWND의 결합 지점을 정리한다.

---

## SessionSendContext

주요 상태는 역할별로 나뉜다.

| 상태 | 역할 | 주요 보호 수단 |
|---|---|---|
| reliable send queue | 신뢰 채널 송신 항목 대기 | queue mutex |
| unreliable queue | 신뢰성 없는 채널 송신 항목 대기·오래된 항목 교체 | reliable queue와 같은 mutex |
| send map | sequence별 ACK 대기 추적 | shared mutex |
| RIO send buffer | 여러 패킷 batch 구성 | send I/O mode와 단일 작성자 계약 |
| cached sequence set | 한 배치 안의 중복 전송 방지 | send I/O mode의 단일 작성자 계약 |
| pending queue | window 부족 시 보류 | 전용 mutex |
| last sequence | sequence 발급 | atomic |

`IO_NONE_SENDING → IO_SENDING` 전이는 동시에 하나의 RIO send만 구성하도록 제한한다. send completion은 mode를 되돌리고 queue에 남은 데이터를 다시 시도한다.

두 송신 큐에 모두 항목이 있으면 큐 선택을 번갈아 수행해 한 채널이 다른 채널을 계속 막지 않게 한다. 신뢰성 없는 큐는 `UNRELIABLE_QUEUE_CAPACITY`를 초과하지 않으며 가득 찬 경우 가장 오래된 미송신 항목을 새 항목으로 교체한다. 이 채널은 ACK map, pending queue, CWND, 재전송 scheduler를 사용하지 않는다.

[상세 코드](SessionComponentsReference.md#6-sessionsendcontext)

---

## ACK와 재전송 수명

```text
SendPacketInfo 생성
  → send map 등록
  → scheduler 등록
  → RIO send
  → ACK 수신: map 제거 + erased 표시
  → heap pop: version/erased 확인
  → 마지막 참조가 Free
```

ACK 수신과 timeout은 서로 다른 thread에서 경쟁할 수 있다. map 존재 여부, heap entry, 실제 객체 수명을 하나의 신호로 간주하지 않는다. schedule version, erased state, ref-count가 서로 일관돼야 한다.

`SendPacketInfo::RequiresRetransmission()`은 reply와 신뢰성 없는 패킷에서 모두 `false`다. 이 항목들은 send completion 뒤 해제되며 ACK map이나 scheduler 참조를 만들지 않는다.

---

## RUDPFlowManager

`RUDPFlowController`의 CWND와 `RUDPReceiveWindow`를 묶어 송신 허용과 수신 수용 범위를 판단한다.

- `CanSend`: 다음 sequence가 현재 송신 window 안인지 확인
- `OnAckReceived`: ACK에 따라 window 진행
- `OnTimeout`: congestion 상태 조정
- `CanAccept` / `MarkReceived`: 수신 sequence window와 중복 관리
- `GetAdvertisableWindow`: 상대에게 알릴 수신 여유 계산

window가 부족하면 packet은 pending queue로 이동하고 ACK 후 flush된다. window 계산과 queue 조작 사이의 경쟁 조건을 막기 위해 판단·등록·flush의 호출 순서를 함께 검토한다.

[상세 코드](SessionComponentsReference.md#7-rudpflowmanager)

---

## 관련 문서

- [세션 컴포넌트 허브](../SessionComponents.md)
- [SendPacketInfo](../SendPacketInfo.md)
- [RetransmissionTimeoutEstimator](../RetransmissionTimeoutEstimator.md)
- [FlowController](../../Common/FlowController.md)
- [RUDPIOHandler](../RUDPIOHandler.md)
- [신뢰성 없는 채널](../../UnreliableChannel.md)
