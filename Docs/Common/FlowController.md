# 흐름 제어 (Flow Controller)

> 현재 코드 기준 흐름 제어 개념만 정리한다. 서버 C++ 구현과 BotTester C# 구현은 목적은 같지만 내부 구조가 동일하지 않다.

---

## 서버 측 핵심

서버에서는 아래 두 구성요소가 핵심이다.

- `RUDPFlowController`: 송신 혼잡 윈도우(CWND) 관리
- `RUDPReceiveWindow`: 수신 윈도우와 advertise window 관리
- `RUDPFlowManager`: 두 객체를 묶고 송신 측 접근을 동기화

ACK를 받으면 송신 가능량을 조정하고, 수신 윈도우는 reorder/holding 상황을 반영해 광고 가능한 여유 공간을 계산한다.

---

## 동시성 계약

`RUDPFlowManager`의 `sendFlowMutex`는 아래 송신 측 연산 전체를 직렬화한다.

- `CanSend()`의 CWND와 마지막 ACK 조합 조회
- `OnAckReceived()`의 ACK 반영
- `OnTimeout()`의 timeout 반영
- `GetCwnd()`와 `Reset()`의 송신 상태 접근

따라서 ACK worker와 timeout·송신 판단이 불완전한 중간 상태를 관찰하지 않는다. 다만 `CanSend()`는 송신 용량을 예약하지 않으므로, 판단 직후 실제 pending queue 등록까지를 원자적으로 보장하지는 않는다.

수신 측 `RUDPReceiveWindow`는 같은 mutex로 보호되지 않는다. `CanAccept()`, `MarkReceived()`, `GetReceiveWindowEnd()`, `GetAdvertisableWindow()`는 receive worker 단일 소유 계약에서 호출해야 하며, `Reset()`과 크기 변경은 세션이 drain된 시점에 수행해야 한다.

---

## C# BotTester 구현과의 관계

예전 문서처럼 C# 구현이 아래 필드를 그대로 갖는다고 보면 안 된다.

- `remoteAdvertisedWindow`
- `TryFlushPendingQueue()`

현재 BotTester C# 세션은 구조가 다르다.

- 순서 보류: `HoldingPacketStore`
- 재전송 추적: `BufferStore`
- 수신 후속 처리: `Channel<Action>` + `PacketProcessorAsync`

즉 개념은 유사하지만, 문서에서 C++ 필드 이름을 C# 구현 상세처럼 서술하지 않는다.

---

## 종료 시그니처 주의

이 문서에 남아 있던 `DoDisconnect()` 무인자 예제는 현재 코드와 맞지 않는다.  
서버 측 `RUDPSession` 종료 요청은 아래처럼 써야 한다.

```cpp
DoDisconnect(DISCONNECT_REASON::BY_ERROR);
```

---

## 운영 관점 정리

- 송신 병목은 CWND, pending queue, 재전송 설정이 함께 만든다.
- 수신 병목은 holding queue 크기와 핸들러 처리 지연이 함께 만든다.
- C# BotTester 문서는 별도 구현 문서로 읽고, 여기서는 공통 개념 위주로 본다.

---

## 관련 문서

- [[RUDPSession]] - 서버 세션 송수신
- [[RudpSession_CS]] - C# 세션 구현
- [[PerformanceTuning]] - 튜닝 포인트
- [[SendAndFlow]] - 세션 송신 큐와 흐름 제어 결합
