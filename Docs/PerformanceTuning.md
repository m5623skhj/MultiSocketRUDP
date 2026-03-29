# 성능 튜닝 가이드

> 서버의 처리량(TPS), 레이턴시, 메모리 효율을 높이기 위한 설정과 코드 패턴을 정리한다.

---

## 목차

1. [스레드 설정 최적화](#1-스레드-설정-최적화)
2. [흐름 제어 튜닝](#2-흐름-제어-튜닝)
3. [재전송 파라미터 튜닝](#3-재전송-파라미터-튜닝)
4. [메모리 최적화](#4-메모리-최적화)
5. [콘텐츠 코드 최적화 패턴](#5-콘텐츠-코드-최적화-패턴)
6. [측정 방법](#6-측정-방법)
7. [환경별 권장 설정](#7-환경별-권장-설정)

---

## 1. 스레드 설정 최적화

### `THREAD_COUNT` (N) 선택 기준

```
IO Worker × N + RecvLogic Worker × N + Retransmission × N
+ Session Release × 1 + Heartbeat × 1 + Ticker × 1 + Logger × 1
+ SessionBroker × (1 + 4)
```

**권장값:**

| CPU 코어 수 | 권장 THREAD_COUNT | 근거 |
|------------|-------------------|------|
| 4 코어 | 2 | IO + Logic = 4 스레드, 나머지는 OS에 |
| 8 코어 | 4 | IO + Logic = 8 스레드 |
| 16 코어 | 6~8 | 코어 수의 50~60% (나머지는 OS/기타용) |
| 32 코어 | 12~16 | HyperThreading 고려 |

> **과다 설정 시:** 컨텍스트 스위칭 비용 증가 → TPS 오히려 감소.  
> **부족 설정 시:** 하나의 스레드가 처리하는 세션 수 과다 → 레이턴시 증가.

### `WORKER_THREAD_ONE_FRAME_MS` 설정

```ini
; 게임 서버 (60fps 목표)
WORKER_THREAD_ONE_FRAME_MS=16

; 채팅/API 서버 (레이턴시 중요하지 않음)
WORKER_THREAD_ONE_FRAME_MS=1

; 최대 처리량 (CPU 100% 허용)
WORKER_THREAD_ONE_FRAME_MS=0    ; 폴링 모드, USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME=0
```

**BuildConfig.h 설정:**
```cpp
#define USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME 1   // sleep 활성
#define USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME 0   // 폴링 모드
```

### 세션-스레드 배정 방식

```cpp
// RUDPSessionManager::Initialize
SetThreadId(*session, i % numOfWorkerThreads);
// → sessionId = i, threadId = i % N
// → 세션 0,N,2N,... → threadId 0
// → 세션 1,N+1,2N+1,... → threadId 1
```

**핫 세션 문제:** 특정 세션에 패킷이 집중되면 해당 threadId의 스레드가 과부하.  
→ `NUM_OF_SOCKET`을 `THREAD_COUNT`의 배수로 설정해 균등 분산.

---

## 2. 흐름 제어 튜닝

> 상세 동작: [[FlowController]]

### CWND (혼잡 윈도우) 파라미터

```cpp
// RUDPFlowController 상수 (소스에서 직접 수정 필요)
constexpr uint8_t INITIAL_CWND = 8;   // 초기 윈도우
constexpr uint8_t MAX_CWND = 64;      // 최대 윈도우
constexpr int32_t GAP_THRESHOLD = 5;  // 혼잡 감지 갭 임계값
```

**튜닝 시나리오:**

```
저레이턴시 요구 (빠른 반응):
  INITIAL_CWND = 16  (초기 버스트 허용)
  MAX_CWND = 128

안정성 우선 (불안정 네트워크):
  INITIAL_CWND = 4
  MAX_CWND = 32
  GAP_THRESHOLD = 3  (더 빨리 혼잡 감지)
```

### 수신 윈도우 크기

```ini
MAX_HOLDING_PACKET_QUEUE_SIZE=64    ; 기본 32 → 대역폭이 충분한 경우 증가
```

> 너무 크면 메모리 소비 증가 (세션당 `MAX_HOLDING_PACKET_QUEUE_SIZE × sizeof(pair)` 바이트).

---

## 3. 재전송 파라미터 튜닝

### `RETRANSMISSION_MS` (재전송 간격)

```
네트워크 RTT가 10ms 이하 (LAN):   RETRANSMISSION_MS=50
일반 인터넷 (RTT 20~100ms):       RETRANSMISSION_MS=200
불안정 네트워크 (RTT 100ms+):     RETRANSMISSION_MS=500
```

> **너무 짧으면:** 불필요한 재전송 → 대역폭 낭비, 수신 측 중복 폐기 증가  
> **너무 길면:** 패킷 유실 복구 지연 → 체감 레이턴시 증가

### `RETRANSMISSION_THREAD_SLEEP_MS`

```ini
; 재전송 스레드가 얼마나 자주 타임아웃을 확인하는가
RETRANSMISSION_THREAD_SLEEP_MS=30   ; 정밀 타이밍 필요
RETRANSMISSION_THREAD_SLEEP_MS=100  ; CPU 절약 우선
```

> `RETRANSMISSION_THREAD_SLEEP_MS < RETRANSMISSION_MS` 이어야 의미 있음.

### `MAX_PACKET_RETRANSMISSION_COUNT`

```ini
; 재전송 횟수 한계 (이 값 초과 시 세션 강제 종료)
MAX_PACKET_RETRANSMISSION_COUNT=10   ; 빠른 감지 (응답 없는 클라이언트 빨리 정리)
MAX_PACKET_RETRANSMISSION_COUNT=20   ; 느린 네트워크 허용

; 총 연결 유지 시간 = RETRANSMISSION_MS × MAX_PACKET_RETRANSMISSION_COUNT
; 예: 200ms × 15회 = 3000ms = 3초 후 강제 종료
```

---

## 4. 메모리 최적화

### TLS 메모리 풀 활용

```
CTLSMemoryPool<SendPacketInfo>   ← lock-free (스레드별 독립 풀)
CTLSMemoryPool<IOContext>        ← lock-free
NetBuffer 풀                     ← 내부 할당
```

> TLS 풀은 별도 설정 없이 자동 사용된다.  
> 풀 초기 크기가 부족하면 동적 확장이 발생 → 첫 실행 시 워밍업 권장.

### `NUM_OF_SOCKET` 설정에 따른 메모리 사용량

대략적인 세션당 메모리:

```
SessionRIOContext:
  recv 버퍼: 16KB (RECV_BUFFER_SIZE)
  send 버퍼: 32KB (MAX_SEND_BUFFER_SIZE)

SessionPacketOrderer:
  HoldingQueue: MAX_HOLDING_PACKET_QUEUE_SIZE × 16 bytes

기타 멤버: ~1KB

총합: ~50KB / 세션
→ 1000 세션: ~50MB
→ 10000 세션: ~500MB
```

### RIO 버퍼 등록 최소화

```
현재: 세션당 4개 RIO 버퍼 등록
  1. recv 데이터 버퍼 (16KB)
  2. 클라이언트 주소 버퍼 (28 bytes)
  3. 로컬 주소 버퍼 (28 bytes)
  4. send 데이터 버퍼 (32KB)
```

RIO 버퍼 등록 수에는 시스템 한계가 있다. Windows에서는 보통 수만 개까지 가능하나,  
세션 수가 매우 많을 경우 `RIORegisterBuffer` 실패 여부를 모니터링해야 한다.

---

## 5. 콘텐츠 코드 최적화 패턴

### 패킷 핸들러 내 작업 최소화

```cpp
// ❌ 느린 패턴: 핸들러 내에서 무거운 작업
void Player::OnMove(const MoveReq& packet) {
    // RecvLogic Worker 스레드를 오래 점유
    auto nearPlayers = SpatialIndex::QueryRadius(packet.x, packet.y, 100.0f);
    for (auto* p : nearPlayers) {
        MoveNotifyPacket notify;
        p->SendPacket(notify);
    }
}

// ✅ 빠른 패턴: 큐에 작업 위임
void Player::OnMove(const MoveReq& packet) {
    // 핸들러에서는 데이터만 저장
    pendingX = packet.x;
    pendingY = packet.y;
    hasNewPosition = true;
    // 실제 처리는 게임 로직 스레드(Ticker, TimerEvent 등)에서
}
```

### 브로드캐스트 최적화

```cpp
// ❌ 비효율: 방 전체에 개별 EncodePacket
void Room::Broadcast(MoveNotify& packet) {
    for (auto* player : members) {
        player->SendPacket(packet);  // 각자 EncodePacket
    }
}

// ✅ 더 나은 방법: 한 번 직렬화 후 재사용
// (현재 구조에서 완전한 zero-copy는 어렵지만, 직렬화만이라도 공유 가능)
// → PacketCryptoHelper는 세션별 다른 키를 사용하므로 암호화 공유 불가
// → 대신 SendPacket 호출 전 PacketToBuffer만 미리 수행하는 패턴 고려
```

### 전송 순서 제어

```cpp
// 순서 의존성 있는 패킷은 이전 ACK 수신 후 전송
// → PendingQueue가 자동으로 순서를 보장하므로 별도 처리 불필요

// 독립적인 패킷은 순서 상관없이 전송 가능
void Player::OnFrame() {
    PositionPacket pos;
    SendPacket(pos);  // 흐름 제어가 알아서 처리
}
```

---

## 6. 측정 방법

### TPS 측정

```cpp
// 서버 측
int32_t tps = core.GetTPS();
core.ResetTPS();

// 패킷 처리 레이턴시 측정 (TimerEvent 활용)
class LatencyMeasure : public TimerEvent {
public:
    LatencyMeasure(TimerEventId id, TimerEventInterval interval)
        : TimerEvent(id, interval) {}
private:
    void Fire() override {
        // 평균 처리 시간 출력
    }
};

auto timer = TimerEventCreator::Create<LatencyMeasure>(1000 /*ms*/);
Ticker::GetInstance().RegisterTimerEvent(timer);
```

### 세션 통계

```cpp
// 연결 수 추이
unsigned short connected = core.GetNowSessionCount();

// 풀 현황 (미사용 세션 수)
// → RUDPSessionManager::GetUnusedSessionCount() 추가 구현 필요
```

### 재전송 모니터링

```cpp
// 재전송 발생 시 LOG_DEBUG가 출력됨
// 로그 파일에서 "Retransmission" 빈도 집계
```

---

## 7. 환경별 권장 설정

### 로컬 개발/테스트

```ini
[CORE]
THREAD_COUNT=2
NUM_OF_SOCKET=100
MAX_PACKET_RETRANSMISSION_COUNT=5
WORKER_THREAD_ONE_FRAME_MS=1
RETRANSMISSION_MS=100
RETRANSMISSION_THREAD_SLEEP_MS=50
HEARTBEAT_THREAD_SLEEP_MS=5000
TIMER_TICK_MS=16
MAX_HOLDING_PACKET_QUEUE_SIZE=32
```

### 소규모 서버 (동시 접속 500 이하, 8코어)

```ini
[CORE]
THREAD_COUNT=4
NUM_OF_SOCKET=512
MAX_PACKET_RETRANSMISSION_COUNT=15
WORKER_THREAD_ONE_FRAME_MS=1
RETRANSMISSION_MS=200
RETRANSMISSION_THREAD_SLEEP_MS=50
HEARTBEAT_THREAD_SLEEP_MS=3000
TIMER_TICK_MS=16
MAX_HOLDING_PACKET_QUEUE_SIZE=32
```

### 대규모 서버 (동시 접속 5000+, 16코어)

```ini
[CORE]
THREAD_COUNT=8
NUM_OF_SOCKET=5120
MAX_PACKET_RETRANSMISSION_COUNT=20
WORKER_THREAD_ONE_FRAME_MS=0       ; 폴링 모드 (USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME=0)
RETRANSMISSION_MS=200
RETRANSMISSION_THREAD_SLEEP_MS=30
HEARTBEAT_THREAD_SLEEP_MS=3000
TIMER_TICK_MS=16
MAX_HOLDING_PACKET_QUEUE_SIZE=64
```

---

## 관련 문서
- [[FlowController]] — CWND 동작 원리
- [[ThreadModel]] — 스레드 구조 상세
- [[MultiSocketRUDPCore]] — 옵션 파일 설정값
- [[TroubleShooting]] — 성능 저하 디버깅
