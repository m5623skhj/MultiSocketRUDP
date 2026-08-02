# 세션 상태·암호화·소켓 컨텍스트

> 세션의 논리 상태와 장기 자원의 초기화·해제 경계를 함께 검토한다.

---

## SessionStateMachine

상태는 `DISCONNECTED`, `RESERVED`, `CONNECTED`, `RELEASING` 순환을 따른다. 조회는 atomic load를 사용하고 경쟁 가능한 전이는 CAS로 제한한다.

| 작업 | 허용 전이 또는 의미 |
|---|---|
| `SetReserved` | 초기화된 미사용 세션을 예약 상태로 설정 |
| `TryTransitionToConnected` | `RESERVED → CONNECTED` |
| `TryTransitionToReleasing` | `RESERVED/CONNECTED → RELEASING` |
| `TryAbortReserved` | heartbeat가 timeout 예약을 release로 전환 |
| `SetDisconnected` | 자원 정리 후 pool 반환 가능한 상태 |

상태를 확인한 뒤 별도 작업을 수행하는 check-then-act는 그 사이 전이가 가능한지 검토한다. 단순 atomic 상태 조회만으로 socket이나 context 수명이 고정되지는 않는다.

[상세 코드](SessionComponentsReference.md#2-sessionstatemachine)

---

## SessionCryptoContext

세션 key, salt, BCrypt key object buffer와 key handle을 소유한다. key handle 생성 비용을 패킷마다 지불하지 않도록 세션 동안 재사용한다. 안전한 수명 계약에서는 `BCryptDestroyKey` 후 buffer를 해제해야 하며, 현재 구현에서는 `Release()`만 이 순서를 지킨다.

수명 순서는 다음과 같다.

```text
key/salt 설정
  → key object buffer 할당
  → key handle 생성
  → 패킷 암복호화에서 재사용
  → key handle 파괴
  → key object buffer 해제
  → key/salt 초기화
```

handle이 buffer를 참조하는 동안 buffer를 먼저 해제하면 안 된다. 로그와 dump에는 key, salt, 평문을 남기지 않는다.

> **현재 구현 주의:** `SessionCryptoContext::Release()`는 위 순서를 지키지만, `Initialize()`는 현재 `keyObjectBuffer`를 먼저 해제한 뒤 `BCryptDestroyKey`를 호출한다. 이 문서의 순서는 안전한 수명 계약을 설명하며, `Initialize()`의 현재 순서는 알려진 코드 불일치다. 코드가 수정되기 전에는 초기화 경로가 이 계약을 충족한다고 가정하지 않는다.

[상세 코드](SessionComponentsReference.md#3-sessioncryptocontext)

---

## SessionSocketContext

socket과 local server port를 보관한다. send/receive는 shared access를, close는 exclusive access를 사용해 I/O 등록 중 socket close와의 race를 제한한다.

`SessionSocketContext::CloseSocket()` 자체는 lock을 획득하지 않는다. 호출자가 `GetSocketMutex()`의 `unique_lock`을 먼저 획득해야 하며, 현재 `RUDPSession::CloseSocket()`이 이 조건을 만족한다. 이미 lock을 보유한 상태에서 내부에서도 다시 잠근다고 가정하면 안 된다.

socket lock은 socket handle 접근만 보호한다. 세션 상태, 원격 주소, RIO context, 완료 callback의 수명은 별도 계약이다. lock을 획득한 채 외부 callback이나 장시간 대기를 추가하면 종료 경로와 deadlock 가능성을 함께 검토한다.

[상세 코드](SessionComponentsReference.md#4-sessionsocketcontext)

---

## 관련 문서

- [세션 컴포넌트 허브](../SessionComponents.md)
- [SessionLifecycle](../SessionLifecycle.md)
- [CryptoSystem](../../Common/CryptoSystem.md)
- [TLSHelper](../../Common/TLSHelper.md)
