# Ticker

> **밀리초 단위 타이머 이벤트 시스템.**  
> 싱글톤 `Ticker`가 `tickInterval`ms마다 등록된 `TimerEvent`들의 발화 여부를 확인하고 `Fire()`를 호출한다.  
> 콘텐츠 서버에서 게임 루프, 주기적 동기화, 지연 실행 등에 사용한다.

---

## 목차

1. [구조 개요](#1-구조-개요)
2. [TimerEvent 클래스](#2-timerevent-클래스)
3. [TimerEventCreator — 생성 헬퍼](#3-timereventcreator--생성-헬퍼)
4. [Ticker 싱글톤](#4-ticker-싱글톤)
5. [이벤트 등록 — RegisterTimerEvent](#5-이벤트-등록--registertimerevent)
6. [이벤트 해제 — UnregisterTimerEvent](#6-이벤트-해제--unregistertimerevent)
7. [내부 루프 — UpdateTick](#7-내부-루프--updatetick)
8. [해제 큐 분리 설계](#8-해제-큐-분리-설계)
9. [콘텐츠 서버 사용 패턴](#9-콘텐츠-서버-사용-패턴)
10. [옵션 설정 (TIMER_TICK_MS)](#10-옵션-설정-timer_tick_ms)

---

## 1. 구조 개요

```
Ticker (싱글톤)
 ├── tickInterval: uint32_t          ← TIMER_TICK_MS 설정값 (ms)
 ├── tickCount: uint64_t             ← 누적 틱 수
 ├── nowMs: uint64_t                 ← 현재 시각 (GetTickCount64())
 ├── timerEvents: map<TimerEventId, shared_ptr<TimerEvent>>
 │    └─ TimerEventId → 등록된 이벤트
 ├── unregisterTargetList: vector<TimerEventId>
 │    └─ Fire() 중 해제 요청된 이벤트 목록 (다음 틱에 일괄 제거)
 ├── tickerThread: jthread            ← UpdateTick 루프 실행
 └── timerEventsLock: shared_mutex   ← 등록/해제/조회 보호
```

---

## 2. TimerEvent 클래스

```cpp
class TimerEvent {
public:
    explicit TimerEvent(TimerEventId id, TimerEventInterval intervalMs)
        : timerEventId(id)
        , interval(intervalMs)
        , nextFireTick(0)
    {}

    virtual ~TimerEvent() = default;

    TimerEventId       GetTimerEventId() const { return timerEventId; }
    TimerEventInterval GetInterval()     const { return interval; }
    uint64_t           GetNextFireTick() const { return nextFireTick; }

    void SetNextTick(uint64_t nowMs) {
        nextFireTick = nowMs + interval;
    }

    bool ShouldFire(uint64_t nowMs) const noexcept {
        return nowMs >= nextFireTick;
    }

private:
    virtual void Fire() = 0;   // ← 콘텐츠 서버가 구현

    friend class Ticker;
    TimerEventId       timerEventId;
    TimerEventInterval interval;
    uint64_t           nextFireTick;
};
```

**타입 정의:**

```cpp
using TimerEventId       = uint32_t;   // 이벤트 식별자 (콘텐츠 서버가 직접 부여)
using TimerEventInterval = uint32_t;   // 발화 간격 (ms)
```

**Fire() 호출 보장:**  
`Fire()`는 항상 Ticker Thread에서 호출된다.  
즉, `Fire()` 내에서 서로 다른 `TimerEvent`들이 동시에 실행되지 않는다.  
단, IO Worker/RecvLogic Worker와의 공유 데이터는 별도 동기화 필요.

---

## 3. TimerEventCreator — 생성 헬퍼

```cpp
class TimerEventCreator {
public:
    template <typename T, typename... Args>
    static std::shared_ptr<T> Create(
        TimerEventInterval intervalMs,
        Args&&... args
    )
    {
        static std::atomic<TimerEventId> nextId{ 1 };
        TimerEventId id = nextId.fetch_add(1, std::memory_order_relaxed);
        return std::make_shared<T>(id, intervalMs, std::forward<Args>(args)...);
    }
};
```

**TimerEventId 자동 할당:**  
`nextId`를 원자적으로 증가시켜 항상 고유한 ID를 보장한다.  
콘텐츠 서버에서 수동으로 ID를 관리할 필요가 없다.

```cpp
// 사용 예시
auto timer = TimerEventCreator::Create<MyGameLoop>(
    16,          // 16ms 간격 (~60fps)
    core,        // MyGameLoop 생성자 추가 파라미터
    roomManager
);
// timer->GetTimerEventId() == 자동 할당된 ID
Ticker::GetInstance().RegisterTimerEvent(timer);
```

---

## 4. Ticker 싱글톤

```cpp
class Ticker {
public:
    static Ticker& GetInstance() {
        static Ticker instance;
        return instance;
    }

    void Start(uint32_t tickIntervalMs) {
        this->tickInterval = tickIntervalMs;
        tickerThread = std::jthread([this](std::stop_token st) {
            UpdateTick(st);
        });
    }

    void Stop() {
        tickerThread.request_stop();
        // jthread 소멸 → join()
    }
};
```

**`MultiSocketRUDPCore::RunAllThreads`에서:**

```cpp
Ticker::GetInstance().Start(timerTickMs);  // 가장 먼저 시작
// ...
// StopServer에서:
Ticker::GetInstance().Stop();              // 가장 나중에 정지
```

---

## 5. 이벤트 등록 — `RegisterTimerEvent`

```cpp
void Ticker::RegisterTimerEvent(std::shared_ptr<TimerEvent> timerEvent)
{
    std::unique_lock lock(timerEventsLock);
    TimerEventId id = timerEvent->GetTimerEventId();

    if (timerEvents.contains(id)) {
        LOG_ERROR(std::format("TimerEvent {} already registered", id));
        return;
    }

    // 다음 발화 시각 초기화 (즉시 발화 방지: 현재 시각 + interval)
    timerEvent->SetNextTick(nowMs);

    timerEvents.emplace(id, std::move(timerEvent));
}
```

**SetNextTick(nowMs) 의미:**

```
nowMs = 현재 시각 (예: 10000ms)
interval = 1000ms

nextFireTick = nowMs + interval = 11000ms
→ 등록 후 1초 뒤 첫 발화 (즉시 발화하지 않음)
```

**thread-safe 접근:**  
`shared_mutex` 사용으로 UpdateTick 루프(읽기)와 Register(쓰기)가 안전하게 공존.

---

## 6. 이벤트 해제 — `UnregisterTimerEvent`

```cpp
void Ticker::UnregisterTimerEvent(TimerEventId timerEventId)
{
    // ① Fire() 실행 중 호출될 수 있음 → 즉시 map에서 제거하면 이터레이터 무효화
    //    → unregisterTargetList에 보관, 다음 틱 시작 시 일괄 제거
    {
        std::unique_lock lock(timerEventsLock);
        unregisterTargetList.push_back(timerEventId);
    }
}
```

**즉시 해제가 아닌 이유:**

```cpp
// UpdateTick 내부
for (auto& [id, event] : timerEvents) {   // ← 이 루프 실행 중
    if (event->ShouldFire(nowMs)) {
        event->Fire();
        // Fire() 내에서 UnregisterTimerEvent(id) 호출 가능!
        // → timerEvents에서 즉시 erase하면 이터레이터 무효화 → UB
    }
}
```

`unregisterTargetList`에 먼저 쌓고, 다음 틱의 `UpdateTick` 시작에서 일괄 처리.

---

## 7. 내부 루프 — `UpdateTick`

```cpp
void Ticker::UpdateTick(const std::stop_token& stopToken)
{
    while (!stopToken.stop_requested()) {
        auto frameStart = std::chrono::steady_clock::now();

        // ① 현재 시각 갱신
        nowMs = GetTickCount64();

        // ② 해제 예정 이벤트 일괄 제거
        UnregisterTimerEventImpl();
        // → unique_lock(timerEventsLock)
        // → for each id in unregisterTargetList: timerEvents.erase(id)
        // → unregisterTargetList.clear()

        // ③ 발화 체크
        {
            std::shared_lock lock(timerEventsLock);  // 읽기 잠금
            for (auto& [id, event] : timerEvents) {
                if (!event->ShouldFire(nowMs)) continue;

                event->Fire();                        // 콘텐츠 로직 실행
                event->SetNextTick(nowMs);            // 다음 발화 시각 갱신
            }
        }

        ++tickCount;

        // ④ 프레임 시간 맞추기 (남은 시간 sleep)
        auto elapsed = std::chrono::steady_clock::now() - frameStart;
        auto remaining = std::chrono::milliseconds(tickInterval) - elapsed;
        if (remaining > std::chrono::milliseconds(0)) {
            std::this_thread::sleep_for(remaining);
        }
    }
}
```

**정밀도 한계:**

```
tickInterval=16ms (60fps 목표):
  Fire() 실행 시간이 길면 → 다음 틱 지연
  Fire() 실행 시간이 0이면 → sleep_for(16ms - ε) ≈ 16ms

sleep_for 정밀도:
  Windows 기본: ~15ms 해상도 (timeBeginPeriod(1)로 1ms까지 향상 가능)
  → 정확한 60fps를 보장하지 않음, 게임 로직은 delta time 기반으로 구현 권장
```

---

## 8. 해제 큐 분리 설계

```
TimerEvent::Fire() 실행 중 UnregisterTimerEvent()를 호출하는 시나리오:

예: 5초 후 1회성 실행 이벤트
  void DelayedEvent::Fire() {
      // 실행...
      Ticker::GetInstance().UnregisterTimerEvent(GetTimerEventId());
      // ↑ 현재 루프를 순회 중이므로 즉시 erase 불가
  }

해결:
  UnregisterTimerEvent() → unregisterTargetList에 push_back
  (현재 루프는 계속 진행)
  
  다음 틱 시작 시 UnregisterTimerEventImpl():
  → for id in unregisterTargetList:
       timerEvents.erase(id)    ← 루프 밖이므로 안전
```

---

## 9. 콘텐츠 서버 사용 패턴

### 반복 실행 이벤트

```cpp
// GameLoop.h
class GameLoopEvent final : public TimerEvent {
public:
    GameLoopEvent(TimerEventId id, TimerEventInterval interval,
                  MultiSocketRUDPCore& core)
        : TimerEvent(id, interval), core(core) {}

private:
    void Fire() override {
        // 모든 방의 상태 업데이트
        RoomManager::GetInstance().UpdateAll();

        // TPS 로그 (주기 조절)
        if (++counter % 60 == 0) {  // 60 ticks = 약 1초
            LOG_DEBUG(std::format("TPS: {}", core.GetTPS()));
            core.ResetTPS();
        }
    }

    MultiSocketRUDPCore& core;
    uint32_t counter = 0;
};

// main.cpp
auto loop = TimerEventCreator::Create<GameLoopEvent>(16, core);
Ticker::GetInstance().RegisterTimerEvent(loop);
```

### 1회성 지연 실행 이벤트

```cpp
class DelayedPacketEvent final : public TimerEvent {
public:
    DelayedPacketEvent(TimerEventId id, TimerEventInterval delay,
                       SessionIdType targetId,
                       MultiSocketRUDPCore& core)
        : TimerEvent(id, delay), targetId(targetId), core(core) {}

private:
    void Fire() override {
        auto* session = core.GetUsingSession(targetId);
        if (session && session->IsConnected()) {
            DelayedPacket pkt;
            session->SendPacket(pkt);
        }
        // 1회 실행 후 자기 자신 해제
        Ticker::GetInstance().UnregisterTimerEvent(GetTimerEventId());
    }

    SessionIdType targetId;
    MultiSocketRUDPCore& core;
};

// 사용
void Player::OnConnected() {
    auto evt = TimerEventCreator::Create<DelayedPacketEvent>(
        5000,          // 5초 후
        GetSessionId(),
        core
    );
    Ticker::GetInstance().RegisterTimerEvent(evt);
}
```

### 주기적 청소 이벤트

```cpp
class CleanupEvent final : public TimerEvent {
public:
    CleanupEvent(TimerEventId id, MultiSocketRUDPCore& core)
        : TimerEvent(id, 30000), core(core) {}  // 30초마다

private:
    void Fire() override {
        // 비활성 룸 정리
        RoomManager::GetInstance().CleanupEmptyRooms();
    }
    MultiSocketRUDPCore& core;
};
```

---

## 10. 옵션 설정 (TIMER_TICK_MS)

```ini
; CoreOption.ini
TIMER_TICK_MS=16    ; 16ms 간격 (~60fps)
```

**권장값:**

| 용도 | TIMER_TICK_MS | 이유 |
|------|---------------|------|
| 60fps 게임 루프 | 16 | 1000/60 ≈ 16.7ms |
| 서버 측 상태 관리 | 50 | 20fps, CPU 절약 |
| 배치 처리 | 100~1000 | 낮은 빈도, 정밀도 불필요 |
| 하트비트 체크 | 1000 | 1초 주기 |

> `Fire()` 내 작업이 `TIMER_TICK_MS`보다 오래 걸리면 다음 틱이 지연된다.  
> 무거운 작업은 별도 스레드에 위임하고 `Fire()`에서는 작업 큐에 추가만 한다.

---

## 관련 문서
- [[MultiSocketRUDPCore]] — `Ticker::Start(timerTickMs)` 호출
- [[ContentServerGuide]] — 콘텐츠 서버에서 Ticker 활용 예시
- [[ThreadModel]] — Ticker Thread 종료 순서
