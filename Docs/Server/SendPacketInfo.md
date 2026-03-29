# SendPacketInfo

> **재전송 추적과 참조 카운팅을 담당하는 핵심 구조체.**  
> CONNECTED 세션에서 전송된 데이터 패킷은 ACK를 받을 때까지 이 구조체로 추적된다.  
> TLS(Thread-Local Storage) 메모리 풀을 사용해 lock-free 할당/해제를 지원한다.

---

## 목차

1. [구조체 필드 전체](#1-구조체-필드-전체)
2. [생명주기 — Alloc에서 Free까지](#2-생명주기--alloc에서-free까지)
3. [참조 카운팅 설계](#3-참조-카운팅-설계)
4. [isErasedPacketInfo — 이중 처리 방지](#4-iserasepacketinfo--이중-처리-방지)
5. [listItor — O(1) 삭제 설계](#5-listitor--o1-삭제-설계)
6. [isReplyType — ACK 패킷 분리](#6-isreplytype--ack-패킷-분리)
7. [TLS 메모리 풀](#7-tls-메모리-풀)
8. [재전송 스레드와의 동시성 시나리오](#8-재전송-스레드와의-동시성-시나리오)

---

## 1. 구조체 필드 전체

```cpp
struct SendPacketInfo
{
    // ─── 핵심 데이터 ──────────────────────────────────────────────
    NetBuffer* buffer = nullptr;
    // 전송할 패킷 버퍼 포인터. EncodePacket으로 이미 AES-GCM 암호화된 상태.
    // 재전송 시 이 버퍼를 그대로 재전송 (재암호화 없음).
    // 같은 Nonce(시퀀스+방향)로 암호화됐으므로 재전송도 일관성 있음.

    RUDPSession* owner = nullptr;
    // 소유 세션 포인터.
    // 재전송 횟수 초과 시 owner->DoDisconnect() 호출에 사용.

    PacketRetransmissionCount retransmissionCount = 0;
    // 재전송 횟수 카운터. 타입: unsigned short 또는 int8_t.
    // RunRetransmissionThread에서 ++retransmissionCount >= maxPacketRetransmissionCount
    // 이면 강제 종료.

    PacketSequence sendPacketSequence = 0;
    // 이 패킷의 시퀀스 번호.
    // sendPacketInfoMap[sendPacketSequence] = this 로 ACK와 대조.
    // EraseSendPacketInfo(sequence) 호출 시 이 값으로 map.erase().

    unsigned long long retransmissionTimeStamp = 0;
    // 다음 재전송 시각 (GetTickCount64() 기준, ms).
    // 초기값: 0 (Initialize에서)
    // 갱신: RefreshRetransmissionSendPacketInfo에서
    //        retransmissionTimeStamp = GetTickCount64() + retransmissionMs

    // ─── 상태 플래그 ──────────────────────────────────────────────
    std::atomic_bool isErasedPacketInfo{ false };
    // ACK 수신 또는 세션 해제로 EraseSendPacketInfo()가 호출됐음을 표시.
    // 재전송 스레드가 copyList 처리 중 ACK가 도착해도 이중 처리 방지.
    // memory_order: acq_rel (쓰기)/acquire (읽기) 사용.

    bool isInSendPacketInfoList = false;
    // sendPacketInfoList[threadId]에 등록된 여부.
    // false이면 listItor가 무효 → EraseSendPacketInfo에서 erase 시도 안 함.

    bool isReplyType = false;
    // true: ACK/Reply 패킷 (SEND_REPLY_TYPE, HEARTBEAT_REPLY_TYPE)
    //       → 재전송 목록 등록 안 함, 손실 시 클라이언트가 재전송 유발
    // false: 데이터 패킷 (SEND_TYPE, HEARTBEAT_TYPE)
    //        → 재전송 목록 등록, ACK 추적 필요

    // ─── 반복자 ───────────────────────────────────────────────────
    std::list<SendPacketInfo*>::iterator listItor;
    // sendPacketInfoList[threadId]에서 이 항목의 위치.
    // std::list<>의 iterator는 erase 후에도 다른 요소에 무효화 영향 없음 → O(1) 삭제.
    // isInSendPacketInfoList=false 상태에서 이 값은 무효(dangling).

    // ─── 참조 카운팅 ──────────────────────────────────────────────
    std::atomic<int8_t> refCount{ 0 };
    // int8_t: -128~127 범위로 충분 (최대 3개 참조자)
    // 0 도달 시 NetBuffer::Free + pool.Free 호출
};
```

---

## 2. 생명주기 — Alloc에서 Free까지

```
[RUDPSession::SendPacketImmediate]

① sendPacketInfoPool->Alloc()       → refCount=0 (초기화 안 됨)
② sendPacketInfo->Initialize(...)   → refCount=1, isErasedPacketInfo=false
③ InsertSendPacketInfo(seq, info)    → refCount=2 (map 참조)
④ core.SendPacket(info)
     → refCount 추가 변경 없음 (SendContext 큐에 포인터만 복사)
     → DoSend → MakeSendStream → RIOSend
⑤ SendIOCompleted
     → io_mode 복원
     → (info는 sendPacketInfoMap에 유지)
⑥ ACK 수신 → OnSendReply(sequence)
     → FindAndEraseSendPacketInfo(sequence) → refCount=1 (map에서 제거)
     → core.EraseSendPacketInfo(info, threadId)
          → sendPacketInfoList[threadId].erase(listItor) → refCount=1
          → SendPacketInfo::Free(info) → refCount=0 → 실제 해제
```

**예외: 재전송 스레드가 먼저 접근하는 경우:**

```
③ 이후 retransmissionTimeStamp 도달

재전송 스레드:
  lock(sendPacketInfoListLock[threadId])
    info->AddRefCount()         → refCount=3
  unlock

  → 처리 중 ACK 도착 가능:
      info->isErasedPacketInfo = true
      core.EraseSendPacketInfo(info, threadId)
          → sendPacketInfoList.erase (refCount 감소 없음, 단지 list에서 제거)
          → Free(info) → refCount=3-1=2 → 아직 해제 안 됨

  → 재전송 스레드 처리 계속:
      isErasedPacketInfo=true 확인 → 재전송 스킵
      Free(info) → refCount=2-1=1

  → sendPacketInfoMap에서 제거 (ACK 처리에서):
      Free(info) → refCount=1-1=0 → 실제 해제
```

---

## 3. 참조 카운팅 설계

```cpp
void SendPacketInfo::AddRefCount()
{
    refCount.fetch_add(1, std::memory_order_acq_rel);
}

static void SendPacketInfo::Free(SendPacketInfo* target)
{
    if (target == nullptr) return;

    // fetch_sub: 이전 값 반환 (감소 전 값)
    if (target->refCount.fetch_sub(1, std::memory_order_release) == 1) {
        // 이전 값이 1이었음 → 이제 0 → 마지막 참조자
        std::atomic_thread_fence(std::memory_order_acquire);
        // ↑ 다른 스레드의 모든 쓰기가 이 시점 전에 완료됐음을 보장

        NetBuffer::Free(target->buffer);        // 패킷 버퍼 반환
        sendPacketInfoPool->Free(target);        // 구조체 반환
    }
    // refCount가 1보다 크면 아직 다른 참조자 있음 → 해제 안 함
}
```

**`release/acquire` 메모리 순서 이유:**

```
스레드 A: info 쓰기 완료 (fetch_sub(..., release))
스레드 B: info 읽기 (fetch_sub 결과 0 확인 후 fence(acquire))
→ B의 acquire fence가 A의 release 이후에 위치함을 보장
→ B가 info를 Free할 때 A의 모든 쓰기가 완료됐음이 보장
```

---

## 4. `isErasedPacketInfo` — 이중 처리 방지

두 가지 상황에서 같은 `SendPacketInfo`에 접근할 수 있다:

```
상황: ACK와 재전송이 거의 동시에 발생

[RecvLogic Worker Thread]          [Retransmission Thread]
  OnSendReply(seq)                   copyList에 info 추가 (refCount++)
  isErasedPacketInfo = true    ←→    isErasedPacketInfo 확인
  EraseSendPacketInfo(info)          if true: 재전송 스킵
  Free(info) → refCount 감소         Free(info) → refCount 감소

결과: 두 번 Free가 호출되지만 refCount 덕분에 실제 해제는 한 번만
      isErasedPacketInfo로 재전송은 스킵됨
```

**`atomic_bool` 이유:**

```cpp
// ACK 처리 (RecvLogic Worker)
info->isErasedPacketInfo.store(true, std::memory_order_release);

// 재전송 스레드
bool erased = info->isErasedPacketInfo.load(std::memory_order_acquire);
// → release/acquire 짝으로 순서 보장
```

---

## 5. `listItor` — O(1) 삭제 설계

`std::list`는 임의 위치 삭제가 O(1)이지만, 삭제할 이터레이터를 알아야 한다.

```cpp
// sendPacketInfoList[threadId]: std::list<SendPacketInfo*>

// 등록 시: listItor 저장
info->listItor = sendPacketInfoList[threadId].insert(
    sendPacketInfoList[threadId].end(), info);
info->isInSendPacketInfoList = true;

// 삭제 시: 이터레이터로 O(1) 삭제
if (info->isInSendPacketInfoList) {
    sendPacketInfoList[threadId].erase(info->listItor);
    info->isInSendPacketInfoList = false;
}
```

**`std::vector`를 사용하지 않는 이유:**

```
vector: erase(pos) → O(N) (뒤 요소를 앞으로 이동)
list:   erase(iter) → O(1) (링크 포인터만 변경)

재전송 스레드가 리스트를 자주 순회하므로 삭제가 빠른 list가 적합.
단, 캐시 지역성은 vector가 더 좋음.
```

---

## 6. `isReplyType` — ACK 패킷 분리

| `isReplyType` | 패킷 예시 | 재전송 목록 등록 | 손실 시 |
|---------------|-----------|-----------------|---------|
| `false` | SEND_TYPE, HEARTBEAT_TYPE | ✅ | 서버가 자동 재전송 |
| `true` | SEND_REPLY_TYPE, HEARTBEAT_REPLY_TYPE | ❌ | 클라이언트가 원본 재전송 |

**ACK 패킷 재전송 추적이 불필요한 이유:**

```
클라이언트가 패킷 N을 보냄 → 서버 처리 → ACK(N) 전송
ACK(N) 유실됨 → 클라이언트가 패킷 N을 재전송
서버: 패킷 N을 다시 받음 → 순서 보장 로직이 중복으로 감지 → 재ACK(N) 전송

→ 클라이언트가 재전송을 유발하므로 서버가 ACK를 재전송 추적할 필요 없음
```

**HEARTBEAT_TYPE은 isReplyType=false인 이유:**

```
HEARTBEAT_TYPE: 서버 → 클라이언트 (데이터처럼 재전송 추적)
  → 클라이언트 응답(HEARTBEAT_REPLY) 없으면 재전송 카운트 증가 → 연결 종료 감지

HEARTBEAT_REPLY_TYPE: 클라이언트 → 서버 (Reply = isReplyType=true)
  → 서버가 추적 불필요 (클라이언트가 HEARTBEAT를 다시 받아 다시 응답)
```

---

## 7. TLS 메모리 풀

```cpp
// SendPacketInfo.cpp
CTLSMemoryPool<SendPacketInfo>* sendPacketInfoPool
    = new CTLSMemoryPool<SendPacketInfo>(
        2,     // 초기 청크 크기 (SendPacketInfo 2개 = 작게 시작)
        true   // 동적 확장 허용
    );
```

**`CTLSMemoryPool<T>` 동작:**

```
스레드 A (IO Worker 0):
  sendPacketInfoPool->Alloc()
  → 스레드 A 전용 청크에서 할당 → 락 없음

스레드 B (IO Worker 1):
  sendPacketInfoPool->Alloc()
  → 스레드 B 전용 청크에서 할당 → 락 없음

→ 서로 다른 청크이므로 동시 접근 시에도 경쟁 없음
→ 단, 스레드 A가 할당하고 스레드 B가 Free하면 스레드 A의 청크로 반환됨
   (TLS 풀의 cross-thread free는 내부적으로 처리됨)
```

**초기 청크 크기를 2로 하는 이유:**

세션당 최소 1개, 최대 수십 개의 `SendPacketInfo`가 동시에 활성화된다.  
첫 접근 시 동적 확장으로 필요한 만큼 늘어나므로, 초기값은 작아도 된다.

---

## 8. 재전송 스레드와의 동시성 시나리오

### 시나리오 1: 정상 ACK 수신

```
[RecvLogic Worker]          [Retransmission Thread]
                             lock(listLock)
                               for info in list:
                                 if timestamp > now: skip  ← 아직 타임아웃 안 됨
                             unlock
                             sleep(retransmissionMs)
OnSendReply(seq):
  FindAndErase(seq) from map
  isErasedPacketInfo = true
  EraseSendPacketInfo:
    lock(listLock)
      erase(listItor)
    unlock
    Free(info) → refCount 감소

                             → 다음 순회 시 info 없음 (erase됨)
```

### 시나리오 2: 재전송 중 ACK 도착

```
[RecvLogic Worker]          [Retransmission Thread]
                             lock(listLock)
                               info->AddRefCount()  → refCount=2
                             unlock
                             (처리 시작)

OnSendReply(seq):
  isErasedPacketInfo = true
  EraseSendPacketInfo:
    lock(listLock)
      erase(listItor)
    unlock
    Free(info) → refCount=2-1=1  (아직 재전송 스레드 참조 중)

                             isErasedPacketInfo.load() → true → 스킵
                             Free(info) → refCount=1-1=0 → 실제 해제
```

### 시나리오 3: 타임아웃 초과 → DoDisconnect

```
[Retransmission Thread]
  info->AddRefCount() → refCount=2
  ++info->retransmissionCount >= max
  info->owner->DoDisconnect()
    → RELEASING 전이
    → PushToDisconnectTargetSession
  Free(info) → refCount=2-1=1

[Session Release Thread]
  session->Disconnect()
    → ForEachAndClearSendPacketInfoMap:
        for each info in map:
          isErasedPacketInfo = true
          core.EraseSendPacketInfo(info, threadId)
            → erase(listItor) (이미 없을 수도 있음, isInSendPacketInfoList 체크)
            → Free(info) → refCount=1-1=0 → 실제 해제
```

---

## 관련 문서
- [[RUDPSession]] — InsertSendPacketInfo, FindAndEraseSendPacketInfo 사용처
- [[MultiSocketRUDPCore]] — EraseSendPacketInfo 구현, sendPacketInfoList[N]
- [[RUDPIOHandler]] — RefreshRetransmissionSendPacketInfo에서 listItor 갱신
- [[ThreadModel]] — 재전송 스레드 전체 흐름
- [[SessionComponents]] — SessionSendContext 내 sendPacketInfoMap
