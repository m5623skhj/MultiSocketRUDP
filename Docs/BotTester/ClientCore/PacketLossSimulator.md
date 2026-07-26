# PacketLossSimulator

> **BotTester의 송신·수신 UDP datagram 손실을 재현 가능하게 시뮬레이션한다.**  
> 방향별 난수 스트림과 lock을 분리해 한 방향의 호출 횟수가 다른 방향의 손실 순서에 영향을 주지 않게 한다.

---

## 목차

1. [생성자](#생성자)
2. [공개 함수](#공개-함수)
3. [동시성 보호](#동시성-보호)

---

## 생성자

```csharp
public sealed class PacketLossSimulator(double inLossRate, int inSeed)
```

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `inLossRate` | `double` | `0.0`에서 `1.0` 사이의 datagram 손실 확률 |
| `inSeed` | `int` | 재현 가능한 난수 스트림의 기준 seed |

수신 방향은 `inSeed`, 송신 방향은 `inSeed ^ 0x9E6779B9`를 사용한다.

> **전제 조건:** 호출 측은 `inLossRate`를 `[0.0, 1.0]` 범위로 전달해야 한다. 클래스 내부에서는 범위를 보정하지 않는다.

---

## 공개 함수

### `SetEnabled`

```csharp
void SetEnabled(bool inEnabled);
```

손실 시뮬레이션을 켜거나 끈다. 생성 직후에는 비활성화 상태다.

### `ShouldDropReceivedDatagram`

```csharp
bool ShouldDropReceivedDatagram();
```

비활성화 상태면 `false`를 반환한다. 활성화 상태면 수신 전용 난수 스트림에서 값을 뽑아 `lossRate`보다 작을 때 `true`를 반환한다.

### `ShouldDropSendingDatagram`

```csharp
bool ShouldDropSendingDatagram();
```

비활성화 상태면 `false`를 반환한다. 활성화 상태면 송신 전용 난수 스트림으로 손실 여부를 판정한다.

---

## 동시성 보호

- `isEnabled`는 `volatile`로 읽고 쓴다.
- `Random`은 스레드 안전하지 않으므로 수신·송신 인스턴스를 각각 별도 `Lock`으로 보호한다.
- 방향별 난수와 lock이 분리되어 송신 판정 호출이 수신 난수 시퀀스를 소비하지 않는다.
- 같은 seed, 손실률, 방향별 호출 순서를 사용하면 같은 판정 순서를 재현한다.

---

## 관련 문서

- [[BotTesterCore]] - RTT 테스트의 손실률과 seed 입력
- [[RudpSession_CS]] - 실제 송수신 경로의 손실 판정
- [[BufferStore]] - 손실 패킷 재전송 추적
- [[Testing]] - 경계 손실률·seed 재현성·병렬 호출 테스트
