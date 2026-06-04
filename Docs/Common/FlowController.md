# 흐름 제어 (Flow Controller)

> 현재 코드 기준 흐름 제어 개념만 정리한다. 서버 C++ 구현과 BotTester C# 구현은 목적은 같지만 내부 구조가 동일하지 않다.

---

## 서버 측 핵심

서버에서는 아래 두 구성요소가 핵심이다.

- `RUDPFlowController`: 송신 혼잡 윈도우(CWND) 관리
- `RUDPReceiveWindow`: 수신 윈도우와 advertise window 관리

ACK를 받으면 송신 가능량을 조정하고, 수신 윈도우는 reorder/holding 상황을 반영해 광고 가능한 여유 공간을 계산한다.

---

## 서버 측에서 문서화해야 하는 것

- ACK 수신 시 CWND 조정
- timeout 시 보수적 축소
- receive window 기반 advertise window 계산
- pending queue와의 연동

이 부분은 현재 C++ 구현과 맞는다.

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
