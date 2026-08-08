# BufferStore

> **BotTester C# 세션의 미응답 송신 패킷을 시퀀스 순서로 추적한다.**  
> `SendPacketInfo`의 타임스탬프·재전송 횟수와 `SortedDictionary` 기반 저장소를 스레드 안전하게 관리한다.

---

## 목차

1. [`SendPacketInfo`](#1-sendpacketinfo)
2. [저장소 API](#2-저장소-api)
3. [재전송 판정](#3-재전송-판정)
4. [동시성 보호](#4-동시성-보호)

---

## 1. `SendPacketInfo`

```csharp
public class SendPacketInfo(NetBuffer inSentBuffer, PacketSequence inPacketSequence)
```

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `inSentBuffer` | `NetBuffer` | ACK 전까지 보관하고 재전송할 인코딩된 패킷 |
| `inPacketSequence` | `PacketSequence` | 저장소 키와 ACK 매칭에 사용하는 시퀀스 |

주요 메서드를 통해 재전송과 관련된 시간 상태를 관리한다. 모든 숫자 상태는 `Interlocked`를 통해 스레드 안전하게 읽고 갱신한다.

### 주요 메서드

- `InitializeSendTimestamp(ulong now)` — 생성 시각과 첫 송신 시각을 초기화한다.
- `RefreshSendPacketInfo(ulong now)` — 최근 송신 시각을 갱신하고 재전송 횟수를 증가시킨다.
- `IsRetransmissionTime(ulong now)` — `RetransmissionTimeoutMs` 경과 여부를 반환한다.

---
## 2. 저장소 API

`BufferStore`는 내부 `SendBufferStore`에 작업을 위임한다.

```csharp
void EnqueueSendBuffer(SendPacketInfo sendPacketInfo);
SendPacketInfo? PeekSendBuffer();
SendPacketInfo? DequeueSendBuffer();
void RemoveSendBuffer(PacketSequence sequence);
SendPacketInfo? RemoveAndGetSendBuffer(PacketSequence sequence);
int GetSendBufferCount();
List<SendPacketInfo> GetAllSendPacketInfos();
bool ContainsPacket(PacketSequence sequence);
SendPacketInfo? GetSendBuffer(PacketSequence sequence);
void Clear();
```

- `PeekSendBuffer()`와 `DequeueSendBuffer()`는 가장 작은 시퀀스의 항목을 대상으로 한다.
- 같은 시퀀스를 다시 추가하면 기존 항목을 새 객체로 교체한다.
- 항목이 없으면 nullable 조회·제거 API는 `null`을 반환한다.
- `GetAllSendPacketInfos()`는 시퀀스 오름차순의 새 `List`를 반환한다. 반환된 리스트를 수정해도 저장소에는 영향을 주지 않는다.

> 저장소는 등록된 `SendPacketInfo`를 강하게 참조하고, `SendPacketInfo`는 내부 `NetBuffer`를 강하게 참조한다. 저장소에서 제거한 뒤 다른 참조가 없으면 두 객체는 GC 대상이 된다.

---

## 3. 재전송 판정

현재 코드 상수는 아래와 같다.

```text
RetransmissionTimeoutMs = 20
RetransmissionMaxCount  = 16
```

`IsRetransmissionTime(now)`는 `now - 최근 송신 시각 >= 20ms`일 때 `true`를 반환한다. `IsExceedMaxRetransmissionCount()`는 재전송 횟수가 16 이상일 때 `true`를 반환한다.

> **전제 조건:** 시간값은 같은 단조 증가 밀리초 clock에서 가져오고, timeout 판정 전에 `InitializeSendTimestamp()`를 호출해야 한다.

---

## 4. 동시성 보호

- `SendBufferStore`의 모든 dictionary 접근은 단일 `Lock`으로 보호한다.
- `SendPacketInfo`의 타임스탬프와 카운터는 `Interlocked`로 보호한다.
- 저장소 스냅샷은 lock 안에서 복사하므로 열거 중 dictionary 변경과 충돌하지 않는다.
- 스냅샷에 포함된 `SendPacketInfo` 객체 자체는 공유되므로 객체 상태는 `Interlocked` API로 접근해야 한다.

---

## 관련 문서

- [[RudpSession_CS]] - 재전송 worker와 ACK 처리
- [[PacketLossSimulator]] - 손실 환경 재전송 검증
- [[Testing]] - BufferStore 및 동시성 유닛 테스트
