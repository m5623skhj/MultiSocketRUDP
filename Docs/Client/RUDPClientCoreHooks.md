# RUDPClientCoreHooks

> `RUDPClientCore`의 protected virtual hook과 테스트 override 동작을 구분한다.

---

## 기본 hook

```cpp
[[nodiscard]]
virtual bool ShouldSendConnectPacketOnStart() const;

[[nodiscard]]
virtual bool ShouldSendReplyToServer(PacketSequence inRecvPacketSequence, unsigned int inPacketId) const;
```

기본 `RUDPClientCore` 구현은 두 함수 모두 `true`를 반환한다.

| 함수 | 기본 동작 |
|------|-----------|
| `ShouldSendConnectPacketOnStart` | `Start()` 후 CONNECT 패킷을 전송한다. |
| `ShouldSendReplyToServer` | 서버 패킷에 대한 reply를 전송한다. |

---

## 테스트 override

`IntegrationTest/TestableRUDPClient`는 통합 테스트 시나리오를 만들기 위해 두 hook을 재정의한다.

| 시나리오 | override 목적 |
|----------|---------------|
| `reserve-timeout` | CONNECT를 보내지 않아 서버 reserved session timeout을 검증한다. |
| `drop-ack` | 데이터 패킷 reply를 비활성화해 서버 retransmission disconnect를 검증한다. |

`ShouldSendReplyToServer`에서 `inPacketId == 0`을 항상 reply하는 동작과 auto-reply 설정을 따르는 동작은 `TestableRUDPClient` 전용이다. 이 조건을 `RUDPClientCore` 기본 동작으로 설명하면 안 된다.

---

## 관련 문서

- [[Testing]] - 통합 테스트 하네스와 테스트 전용 클라이언트
- [[RUDPClientCore]] - 클라이언트 코어 수명주기와 송수신 흐름
