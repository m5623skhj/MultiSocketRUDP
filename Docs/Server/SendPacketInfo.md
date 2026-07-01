# SendPacketInfo

> **재전송 추적, RTT 샘플링, 참조 카운팅을 담당하는 핵심 구조체.**
> CONNECTED 세션에서 전송된 데이터 패킷은 ACK를 받을 때까지 이 구조체로 추적된다.
> TLS(Thread-Local Storage) 메모리 풀을 사용해 할당/해제를 수행한다.

---

## 목차

1. [구조체 필드 전체](#1-구조체-필드-전체)
2. [생명주기 — Alloc에서 Free까지](#2-생명주기--alloc에서-free까지)
3. [참조 카운팅 설계](#3-참조-카운팅-설계)
4. [isErasedPacketInfo — 이중 처리 방지](#4-iserasedpacketinfo--이중-처리-방지)
5. [scheduleVersion — stale heap entry 방지](#5-scheduleversion--stale-heap-entry-방지)
6. [isReplyType — ACK 패킷 분리](#6-isreplytype--ack-패킷-분리)
7. [RTT 샘플링](#7-rtt-샘플링)
8. [재전송 스레드와의 동시성 시나리오](#8-재전송-스레드와의-동시성-시나리오)

---

## 1. 구조체 필드 전체

```cpp
struct SendPacketInfo
{
    NetBuffer* buffer{};
    RUDPSession* owner{};
    uint32_t ownerGeneration{};
    PacketRetransmissionCount retransmissionCount{};
    PacketSequence sendPacketSequence{};
    uint64_t scheduleVersion{};
    std::atomic_bool isErasedPacketInfo{};
    bool isReplyType{};
    std::atomic_int32_t refCount{};
    mutable std::mutex rttSampleLock;
    std::chrono::steady_clock::time_point lastSendTime{};
    std::atomic_bool canUseRttSample{};
};
```

| 필드 | 역할 |
|------|------|
| `buffer` | 전송할 암호화된 패킷 버퍼. 재전송 시 같은 버퍼를 재사용한다. |
| `owner` | 패킷을 소유한 세션. 재전송 실패 시 disconnect 처리에 사용한다. |
| `ownerGeneration` | 세션 재사용 후 오래된 패킷이 새 세션을 건드리지 않도록 세션 generation을 저장한다. |
| `retransmissionCount` | 재전송 횟수. `maxPacketRetransmissionCount` 이상이면 `BY_RETRANSMISSION`으로 disconnect한다. |
| `sendPacketSequence` | ACK와 매칭할 패킷 시퀀스 번호. |
| `scheduleVersion` | 재전송 heap에 같은 패킷이 여러 번 들어갈 때 최신 schedule만 유효하게 구분한다. |
| `isErasedPacketInfo` | ACK 수신 또는 세션 해제로 추적 대상에서 제거됐음을 표시한다. |
| `isReplyType` | ACK/Reply 패킷 여부. reply 패킷은 재전송 schedule에 등록하지 않는다. |
| `refCount` | map, send queue, retransmission heap entry가 공유하는 수명 참조 카운터. |
| `rttSampleLock` | `lastSendTime` 읽기/쓰기를 보호한다. |
| `lastSendTime` | 가장 최근 실제 송신 시각. RTT 샘플 계산에 사용한다. |
| `canUseRttSample` | 재전송이 발생하지 않은 패킷만 RTO 추정 샘플로 사용하기 위한 플래그. |

---

## 2. 생명주기 — Alloc에서 Free까지

```
[RUDPSession::SendPacketImmediate]

1. sendPacketInfoPool->Alloc()
2. Initialize(owner, ownerGeneration, buffer, sequence, isReplyType)
   - refCount = 1
   - canUseRttSample = !isReplyType
3. InsertSendPacketInfo(sequence, info)
   - sendPacketInfoMap에 ACK 대기 항목으로 저장
   - map 참조를 위해 AddRefCount()
4. core.SendPacket(info)
   - 세션 send queue 또는 reserved slot을 거쳐 DoSend에서 전송
5. RUDPIOHandler::RefreshRetransmissionSendPacketInfo()
   - reply 패킷이면 schedule하지 않음
   - 데이터 패킷이면 scheduler heap에 deadline/version/info를 push
   - heap entry 참조를 위해 AddRefCount()
6. ACK 수신
   - FindAndEraseSendPacketInfo(sequence)
   - core.MarkSendPacketInfoErased(info, threadId)
   - SendPacketInfo::Free(info)
7. 마지막 참조가 사라지면
   - NetBuffer::Free(buffer)
   - sendPacketInfoPool->Free(info)
```

재전송 heap은 `std::priority_queue` 기반이다. heap entry는 제거가 아니라 새 entry를 추가하는 방식으로 갱신되며, 오래된 entry는 pop 시 `scheduleVersion` 비교로 폐기한다.

---

## 3. 참조 카운팅 설계

```cpp
void SendPacketInfo::AddRefCount()
{
    const int32_t prev = refCount.fetch_add(1, std::memory_order_relaxed);
    if (prev <= 0) {
        LOG_ERROR(...);
    }
}

void SendPacketInfo::Free(SendPacketInfo* target)
{
    if (target == nullptr) return;

    const int32_t prev = target->refCount.fetch_sub(1, std::memory_order_acq_rel);
    if (prev <= 0) {
        LOG_ERROR(...);
        return;
    }

    if (prev == 1) {
        NetBuffer::Free(target->buffer);
        sendPacketInfoPool->Free(target);
    }
}
```

`refCount`는 현재 `std::atomic_int32_t`다. 이전 문서의 `int8_t` 설명은 현재 코드 기준이 아니다.

참조자는 대표적으로 다음과 같다.

| 참조자 | 설명 |
|--------|------|
| 생성/호출자 | `Initialize()` 직후의 기본 참조 |
| `sendPacketInfoMap` | ACK 수신 전까지 sequence로 찾기 위한 참조 |
| 송신 큐/reserved slot | 실제 RIO send stream에 실릴 때까지 유지되는 참조 |
| 재전송 heap entry | deadline에 도달했을 때 재전송 처리를 위해 유지되는 참조 |

---

## 4. `isErasedPacketInfo` — 이중 처리 방지

두 가지 상황에서 같은 `SendPacketInfo`에 접근할 수 있다.

```
[RecvLogic Worker]             [Retransmission Thread]
ACK 수신                         heap top pop
MarkSendPacketInfoErased()        isErasedPacketInfo 확인
Free(info)                        true이면 stale entry로 보고 Free(info)
```

재전송 thread는 heap에서 꺼낸 entry가 다음 조건 중 하나에 해당하면 stale로 보고 재전송하지 않는다.

```cpp
if (top.info->isErasedPacketInfo.load(std::memory_order_acquire) ||
    top.version != top.info->scheduleVersion)
{
    SendPacketInfo::Free(top.info);
    continue;
}
```

---

## 5. `scheduleVersion` — stale heap entry 방지

재전송 deadline은 패킷이 실제 send stream에 실릴 때마다 새로 계산된다.

```cpp
inline void PushRetransmissionSchedule(
    RetransmissionScheduler& scheduler,
    SendPacketInfo& sendPacketInfo,
    std::chrono::steady_clock::time_point deadline)
{
    ++sendPacketInfo.scheduleVersion;
    sendPacketInfo.AddRefCount();
    scheduler.heap.push({ deadline, sendPacketInfo.scheduleVersion, &sendPacketInfo });
}
```

`std::priority_queue`는 중간 entry 삭제가 어렵다. 따라서 기존 entry를 지우지 않고 새 entry를 추가하며, 이전 entry는 pop 시 version mismatch로 폐기한다.

```
version 1 entry push
재전송 또는 재송신으로 version 2 entry push

heap pop version 1:
  top.version != info.scheduleVersion
  → stale entry
  → Free(info)

heap pop version 2:
  최신 entry
  → ProcessRetransmission(info)
```

---

## 6. `isReplyType` — ACK 패킷 분리

| `isReplyType` | 패킷 예시 | 재전송 schedule | 손실 시 |
|---------------|-----------|-----------------|---------|
| `false` | `SEND_TYPE`, `HEARTBEAT_TYPE` | 등록 | 서버가 재전송하고 한계 초과 시 disconnect |
| `true` | `SEND_REPLY_TYPE`, `HEARTBEAT_REPLY_TYPE` | 미등록 | 상대가 원본을 재전송하면 다시 reply |

ACK 패킷을 서버가 별도로 재전송 추적하지 않는 이유는 원본 패킷 송신자가 ACK 유실을 감지해 원본을 재전송하기 때문이다.

---

## 7. RTT 샘플링

재전송 timeout 추정은 재전송되지 않은 데이터 패킷의 RTT만 사용한다.

```cpp
void MarkSentForRttSample(std::chrono::steady_clock::time_point now);
void InvalidateRttSample();
bool TryGetRttSample(std::chrono::steady_clock::time_point now,
                     std::chrono::steady_clock::duration& outSample) const;
```

동작 기준:

- `Initialize()`는 데이터 패킷에 대해서만 `canUseRttSample = true`로 둔다.
- `RefreshRetransmissionSendPacketInfo()`는 실제 송신 시각을 `MarkSentForRttSample(now)`로 기록한다.
- 재전송 timeout이 발생하면 `InvalidateRttSample()`로 해당 패킷의 RTT 샘플을 폐기한다.
- ACK 수신 시 `TryGetRttSample()`이 성공하면 세션의 RTO estimator에 샘플을 반영한다.

---

## 8. 재전송 스레드와의 동시성 시나리오

### 시나리오 1: 정상 ACK 수신

```
RUDPIOHandler
  → PushRetransmissionSchedule(deadline, version=N)

RecvLogic Worker
  → ACK 수신
  → sendPacketInfoMap에서 제거
  → core.MarkSendPacketInfoErased(info, threadId)
  → Free(info)

Retransmission Thread
  → heap에서 entry pop
  → isErasedPacketInfo == true 확인
  → stale entry로 폐기
  → Free(info)
```

### 시나리오 2: schedule 갱신 후 오래된 heap entry 도착

```
첫 송신:
  scheduleVersion = 1
  heap push(version=1)

다시 송신:
  scheduleVersion = 2
  heap push(version=2)

Retransmission Thread:
  pop(version=1)
  version != scheduleVersion
  → stale entry로 폐기
```

### 시나리오 3: 타임아웃 초과 → DoDisconnect

```
Retransmission Thread
  → 최신 heap entry pop
  → ++retransmissionCount >= maxPacketRetransmissionCount
  → owner가 generation까지 유효하면
       owner->DoDisconnect(DISCONNECT_REASON::BY_RETRANSMISSION)
  → Free(info)

Session Release Thread
  → PushToDisconnectTargetSession으로 받은 세션 정리
  → sendPacketInfoMap의 잔여 info를 MarkSendPacketInfoErased 처리
  → 각 info Free
```

---

## 관련 문서
- [[RUDPSession]] — `InsertSendPacketInfo`, `FindAndEraseSendPacketInfo`, RTT 샘플 반영
- [[MultiSocketRUDPCore]] — `RunRetransmissionThread`, `ProcessRetransmission`
- [[RUDPIOHandler]] — `RefreshRetransmissionSendPacketInfo`
- [[ThreadModel]] — 재전송 스레드 전체 흐름
- [[SessionComponents]] — `SessionSendContext` 내 `sendPacketInfoMap`
