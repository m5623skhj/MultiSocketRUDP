# 흐름 제어 (Flow Controller)

> **RUDP에서 패킷 유실·혼잡을 관리하는 두 가지 메커니즘.**  
> `RUDPFlowController`는 송신 측 CWND를 관리하고,  
> `RUDPReceiveWindow`는 수신 측 윈도우와 advertiseWindow를 관리한다.  
> 두 클래스는 `RUDPFlowManager`로 통합되어 각 `RUDPSession`에서 사용된다.

---

## 목차

1. [왜 흐름 제어가 필요한가](#1-왜-흐름-제어가-필요한가)
2. [RUDPFlowController — 송신 혼잡 제어](#2-rudpflowcontroller--송신-혼잡-제어)
3. [RUDPReceiveWindow — 수신 윈도우](#3-rudpreceivewindow--수신-윈도우)
4. [RUDPFlowManager — 통합 인터페이스](#4-rudpflowmanager--통합-인터페이스)
5. [PendingQueue와의 연동](#5-pendingqueue와의-연동)
6. [advertiseWindow — 수신 역압력](#6-advertisewindow--수신-역압력)
7. [파라미터 설정 가이드](#7-파라미터-설정-가이드)
8. [디버그: 흐름 제어 동작 확인](#8-디버그-흐름-제어-동작-확인)

---

## 1. 왜 흐름 제어가 필요한가

UDP 자체는 혼잡 제어가 없다. 패킷을 무제한 전송하면:

```
1. 수신 측 처리 속도 < 전송 속도
   → 수신 버퍼 초과 → 패킷 유실

2. 유실된 패킷 재전송
   → 대역폭 낭비 + 재전송도 유실

3. 혼잡 붕괴 (Congestion Collapse)
   → 재전송만 하고 정작 유효 데이터는 전달 안 됨
```

**TCP의 AIMD 방식을 채용:**
- **Additive Increase**: ACK 수신마다 CWND +1
- **Multiplicative Decrease**: 유실 감지 시 CWND ÷ 2

---

## 2. RUDPFlowController — 송신 혼잡 제어

### 상태 변수

```cpp
class RUDPFlowController {
    uint8_t cwnd;                    // 혼잡 윈도우 크기 (패킷 수)
    bool inRecovery;                 // 혼잡 복구 중 여부
    PacketSequence lastReplySequence; // 마지막 수신 ACK 시퀀스
    PacketSequence initialSequence;   // 세션 시작 시 기준 시퀀스

    // 상수 (소스 직접 수정)
    static constexpr uint8_t INITIAL_CWND = 8;
    static constexpr uint8_t MAX_CWND = 64;
    static constexpr int32_t GAP_THRESHOLD = 5;  // 혼잡 감지 임계값
};
```

### `CanSendPacket` — 전송 가능 여부

```cpp
bool CanSendPacket(PacketSequence nextSendSeq,
                   PacketSequence lastAckedSeq) const noexcept
{
    // outstanding = 아직 ACK 안 받은 패킷 수
    int32_t diff = static_cast<int32_t>(nextSendSeq - lastAckedSeq);
    uint8_t outstanding = diff > 1 ? static_cast<uint8_t>(diff - 1) : 0;
    return outstanding < cwnd;
}
```

**예시 시나리오:**

```
cwnd = 4, lastAcked = 10

nextSend=11: outstanding=0  → ✅ 전송 (패킷 11)
nextSend=12: outstanding=1  → ✅ 전송 (패킷 12)
nextSend=13: outstanding=2  → ✅ 전송 (패킷 13)
nextSend=14: outstanding=3  → ✅ 전송 (패킷 14)
nextSend=15: outstanding=4  → ❌ 전송 불가 → PendingQueue 보관

패킷 11 ACK 수신: lastAcked=11
nextSend=15: outstanding=3  → ✅ PendingQueue에서 꺼내 전송
```

### `OnReplyReceived` — ACK 수신 시 CWND 증가 (AIMD)

```cpp
void OnReplyReceived(PacketSequence replySequence)
{
    if (replySequence <= lastReplySequence) return;  // 중복 ACK

    // Gap 계산: 몇 개의 시퀀스를 건너뛰었는가
    int32_t gap = static_cast<int32_t>(replySequence - lastReplySequence) - 1;

    if (gap >= GAP_THRESHOLD) {
        // 패킷 유실 추정 → 혼잡 이벤트
        OnCongestionEvent();
        // cwnd = max(cwnd / 2, 1)
        // inRecovery = true
    } else if (!inRecovery) {
        // 정상: CWND 증가
        cwnd = static_cast<uint8_t>(std::min(
            static_cast<int>(cwnd) + 1,
            static_cast<int>(MAX_CWND)));
    }

    lastReplySequence = replySequence;
}
```

**Gap 계산 설명:**

```
패킷 전송 순서: 1, 2, 3, 4, 5
ACK 수신 순서 (정상): 1, 2, 3, 4, 5
  gap = 5-4-1 = 0 → 정상

패킷 3이 유실됨:
  패킷 4, 5 먼저 도착 → 홀딩 큐 보관
  패킷 3 도착 → 처리 → ACK 순서: 1, 2, 3=5 (순서 보장 후 일괄 ACK)
  gap = 5-2-1 = 2 (GAP_THRESHOLD=5 미만) → 혼잡 이벤트 없음

여러 패킷이 한꺼번에 유실:
  ACK 순서: 1, 2, 8
  gap = 8-2-1 = 5 (≥ GAP_THRESHOLD) → 혼잡 이벤트
```

> `GAP_THRESHOLD = 5`는 순서가 뒤바뀐 정상 패킷과 진짜 유실을 구분하기 위한 임계값이다.  
> 홀딩 큐에서 처리된 패킷들은 연속 ACK를 발생시켜 gap이 작게 나타난다.

### `OnCongestionEvent` / `OnTimeout`

```cpp
void OnCongestionEvent() noexcept {
    cwnd = std::max(static_cast<int>(cwnd) / 2, 1);
    inRecovery = true;
}

void OnTimeout() noexcept {
    // 재전송 타임아웃: 더 심각한 상황
    cwnd = 1;
    inRecovery = true;
}
```

**`inRecovery` 플래그:**  
복구 중에는 추가 혼잡 이벤트로 CWND가 0이 되는 것을 방지한다.  
다음 정상 ACK 수신 시 `inRecovery = false`로 초기화해 CWND 증가를 재개한다.

```cpp
} else {
    // inRecovery 상태에서 첫 정상 ACK → 복구 완료
    inRecovery = false;
    // (이번 ACK에서는 CWND 증가 안 함, 다음 ACK부터 증가)
}
```

---

## 3. RUDPReceiveWindow — 수신 윈도우

수신 측에서 허용하는 시퀀스 범위를 관리한다.

### 상태 변수

```cpp
class RUDPReceiveWindow {
    PacketSequence windowStart;     // 현재 기대하는 최소 시퀀스
    uint8_t windowSize;             // 윈도우 크기 (옵션 MAX_HOLDING_PACKET_QUEUE_SIZE)
    std::vector<bool> receivedMask; // [i] = (windowStart + i)를 수신했는가
    uint8_t receivedCount;          // 수신된 시퀀스 수
};
```

### `CanReceive` — 수신 허용 여부

```cpp
bool CanReceive(PacketSequence sequence) const noexcept
{
    int32_t diff = static_cast<int32_t>(sequence - windowStart);
    return diff >= 0 && static_cast<uint8_t>(diff) < windowSize;
}
```

```
windowStart=10, windowSize=8 → 허용 범위: [10, 11, 12, 13, 14, 15, 16, 17]

sequence=10: diff=0  → ✅
sequence=17: diff=7  → ✅
sequence=18: diff=8  → ❌ (윈도우 밖)
sequence=9:  diff=-1 → ❌ (이미 지난 시퀀스)
```

### `MarkReceived` — 수신 마킹 + 슬라이딩

```cpp
void MarkReceived(PacketSequence sequence)
{
    uint8_t idx = static_cast<uint8_t>(sequence - windowStart);
    if (receivedMask[idx]) return;  // 이미 마킹됨

    receivedMask[idx] = true;
    ++receivedCount;

    // 앞부분이 연속으로 수신됐으면 윈도우 슬라이딩
    while (receivedMask[0]) {
        // 원형 버퍼 방식으로 슬라이딩
        receivedMask[0] = false;
        std::rotate(receivedMask.begin(), receivedMask.begin() + 1, receivedMask.end());
        ++windowStart;
        --receivedCount;
    }
}
```

**슬라이딩 예시:**

```
초기: windowStart=10, receivedMask=[0,0,0,0,0,0,0,0]

MarkReceived(10): mask=[1,0,0,0,0,0,0,0]
  → 앞이 1이므로 슬라이딩: windowStart=11, mask=[0,0,0,0,0,0,0,0]

MarkReceived(12): mask=[0,1,0,0,0,0,0,0]
  → 앞이 0이므로 슬라이딩 안 함

MarkReceived(11): mask=[1,1,0,0,0,0,0,0]
  → 앞이 1이므로 슬라이딩: windowStart=12, mask=[1,0,0,0,0,0,0,0]
  → 앞이 1이므로 슬라이딩: windowStart=13, mask=[0,0,0,0,0,0,0,0]
```

### `GetAdvertisableWindow` — 광고 윈도우 크기

```cpp
uint8_t GetAdvertisableWindow() const noexcept {
    return windowSize - receivedCount;
    // 수신 가능한 슬롯 수 = 윈도우 크기 - 이미 채워진 슬롯
}
```

---

## 4. RUDPFlowManager — 통합 인터페이스

`RUDPSession`에서 직접 접근하는 통합 클래스.

```cpp
class RUDPFlowManager {
    RUDPFlowController sendController;      // 송신 CWND 관리
    RUDPReceiveWindow  receiveWindow;        // 수신 윈도우 관리
    PacketSequence     lastSendAckedSeq;     // sendController에 전달할 lastAcked
public:
    // 초기화
    void Reset(PacketSequence initialSeq);

    // 송신 제어
    bool CanSend(PacketSequence nextSeq) const noexcept;
    void OnAckReceived(PacketSequence ackedSeq);
    void OnCongestionEvent() noexcept;
    void OnTimeout() noexcept;

    // 수신 제어
    bool CanAccept(PacketSequence sequence) const noexcept;
    void MarkReceived(PacketSequence sequence);
    uint8_t GetAdvertisableWindow() const noexcept;
};
```

### `Reset` — 세션 연결 시 초기화

```cpp
void Reset(PacketSequence initialSeq) {
    sendController.Reset(initialSeq);
    receiveWindow.Reset(initialSeq + 1, windowSize);
    lastSendAckedSeq = initialSeq;
}
```

`TryConnect()` 내에서 `LOGIN_PACKET_SEQUENCE(=0)` + 1로 초기화.

### `CanSend`

```cpp
bool CanSend(PacketSequence nextSeq) const noexcept {
    return sendController.CanSendPacket(nextSeq, lastSendAckedSeq);
}
```

### `OnAckReceived`

```cpp
void OnAckReceived(PacketSequence ackedSeq) {
    lastSendAckedSeq = ackedSeq;
    sendController.OnReplyReceived(ackedSeq);
}
```

---

## 5. PendingQueue와의 연동

흐름 제어가 `PendingQueue`를 어떻게 채우고 비우는지:

```
[SendPacket 호출]
  scoped_lock(pendingQueueLock)
  
  if !pendingQueueEmpty:
    → pendingQueue.push({sequence, buffer})  ← 앞에 대기 중이면 무조건 보관
    return true
    
  if !flowManager.CanSend(sequence):
    → pendingQueue.push({sequence, buffer})  ← 윈도우 가득 참
    return true
  
  // CanSend=true, queue 비어있음 → 즉시 전송
  SendPacketImmediate(buffer, sequence, false, false)

[ACK 수신 → OnSendReply]
  flowManager.OnAckReceived(ackedSeq)  ← lastAcked 갱신, CWND 증가
  TryFlushPendingQueue()
    scoped_lock(pendingQueueLock)
    while !pendingQueueEmpty:
      nextSeq = pendingQueueFront().first
      if !flowManager.CanSend(nextSeq) → break
      pop → sendList
    
    // Lock 해제
    for each in sendList:
      SendPacketImmediate(...)
```

**PendingQueue가 꽉 찼을 때 (`RingBuffer` 초과):**

```cpp
bool RUDPSession::SendPacket(IPacket& packet) {
    // ...
    bool pushed = rioContext.GetSendContext().PushToPendingQueue(seq, &buffer);
    if (!pushed) {
        // 큐가 가득 참 → 클라이언트가 너무 느림
        DoDisconnect();
        return false;
    }
    return true;
}
```

---

## 6. advertiseWindow — 수신 역압력

서버가 클라이언트의 전송 속도를 제한하는 메커니즘.

```
서버 수신 처리 속도가 느릴 때:
  receivedCount 증가 → advertiseWindow 감소
  ACK(SEND_REPLY_TYPE)에 작은 advertiseWindow 포함
  클라이언트가 이 값을 받아 전송 속도 줄임
  
서버 처리 속도 회복 시:
  MarkReceived → windowStart 슬라이딩 → receivedCount 감소
  → advertiseWindow 증가 → 클라이언트 전송 속도 회복
```

**C# 클라이언트에서 advertiseWindow 처리:**

```csharp
// RudpSession_CS.cs
void OnSendReply(NetBuffer recvPacket) {
    ulong ackedSeq;
    byte remoteAdvertisedWindow;
    recvPacket >> ackedSeq;
    recvPacket >> remoteAdvertisedWindow;

    this.remoteAdvertisedWindow = remoteAdvertisedWindow;
    // SendPacket에서 이 값을 확인
    TryFlushPendingQueue();
}

bool CanSendMore() {
    int outstanding = sendPacketInfoMap.Count;
    return outstanding < remoteAdvertisedWindow;
}
```

---

## 7. 파라미터 설정 가이드

### CWND 상수 (소스 직접 수정)

```cpp
// FlowController.h 또는 RUDPFlowController.cpp
constexpr uint8_t INITIAL_CWND  = 8;   // 세션 연결 시 초기 윈도우
constexpr uint8_t MAX_CWND      = 64;  // 최대 윈도우
constexpr int32_t GAP_THRESHOLD = 5;   // 혼잡 감지 갭 임계값
```

| 시나리오 | INITIAL_CWND | MAX_CWND | GAP_THRESHOLD |
|----------|-------------|----------|---------------|
| LAN 환경 (저레이턴시) | 16 | 128 | 3 |
| 일반 인터넷 | 8 | 64 | 5 |
| 불안정 네트워크 | 4 | 32 | 3 |
| 대용량 데이터 전송 | 8 | 256 | 8 |

### 수신 윈도우 크기 (옵션 파일)

```ini
MAX_HOLDING_PACKET_QUEUE_SIZE=32   ; 수신 윈도우 = 홀딩 큐 크기
```

> 수신 윈도우 크기는 `advertiseWindow`로 클라이언트에게 알려지므로  
> 송신 CWND와 수신 윈도우 중 작은 값이 실질적인 전송 속도를 결정한다.

**두 윈도우의 상호작용:**

```
CWND=64, advertiseWindow=8:
  클라이언트는 min(64, 8)=8개까지만 전송 가능
  → 수신 윈도우가 병목

CWND=4, advertiseWindow=32:
  클라이언트는 min(4, 32)=4개까지 전송 가능
  → 혼잡 윈도우가 병목
```

---

## 8. 디버그: 흐름 제어 동작 확인

### PendingQueue 가득 참 (DoDisconnect 빈번)

```
원인: 클라이언트가 ACK를 전혀 보내지 않음
     → CWND가 증가 안 됨 → PendingQueue 가득 참 → DoDisconnect

확인:
  - HEARTBEAT 전송 후 클라이언트의 HEARTBEAT_REPLY 수신 여부
  - Retransmission Thread의 재전송 빈도
```

### advertiseWindow=0 상태 지속

```
원인: 서버 수신 처리 속도 < 클라이언트 전송 속도
     → receivedMask가 가득 참 → GetAdvertisableWindow()=0

해결:
  - RecvLogic Worker 수 증가 (THREAD_COUNT)
  - OnRecvPacket 핸들러 내 병목 제거
  - MAX_HOLDING_PACKET_QUEUE_SIZE 증가
```

### 혼잡 이벤트 과다 (CWND 자주 감소)

```
원인: 네트워크 패킷 유실 또는 재전송이 잦음
     → gap >= GAP_THRESHOLD 자주 발생

확인:
  - Retransmission Thread 로그 "Retransmission" 빈도
  - GAP_THRESHOLD 증가로 오탐 줄이기
  - RETRANSMISSION_MS 조정
```

---

## 관련 문서
- [[RUDPSession]] — CanSend/OnAckReceived/MarkReceived 호출 시점
- [[PacketProcessing]] — advertiseWindow가 포함된 SEND_REPLY_TYPE 처리
- [[PacketFormat]] — SEND_REPLY_TYPE 페이로드 구조
- [[PerformanceTuning]] — CWND/윈도우 파라미터 설정
- [[TroubleShooting]] — advertiseWindow=0 디버깅
