# Ticker

> **諛由ъ큹 ?⑥쐞 ??대㉧ ?대깽???쒖뒪??**  
> ?깃???`Ticker`媛 `tickInterval`ms留덈떎 ?깅줉??`TimerEvent`?ㅼ쓽 諛쒗솕 ?щ?瑜??뺤씤?섍퀬 `Fire()`瑜??몄텧?쒕떎.  
> 肄섑뀗痢??쒕쾭?먯꽌 寃뚯엫 猷⑦봽, 二쇨린???숆린?? 吏???ㅽ뻾 ?깆뿉 ?ъ슜?쒕떎.

---

## 紐⑹감

1. [援ъ“ 媛쒖슂](#1-援ъ“-媛쒖슂)
2. [TimerEvent ?대옒??(#2-timerevent-?대옒??
3. [TimerEventCreator ???앹꽦 ?ы띁](#3-timereventcreator--?앹꽦-?ы띁)
4. [?⑥닔 ?ㅻ챸](#4-?⑥닔-?ㅻ챸)
5. [Ticker ?깃???(#5-ticker-?깃???
6. [?대깽???깅줉 ??RegisterTimerEvent](#6-?대깽???깅줉--registertimerevent)
7. [?대깽???댁젣 ??UnregisterTimerEvent](#7-?대깽???댁젣--unregistertimerevent)
8. [?대? 猷⑦봽 ??UpdateTick](#8-?대?-猷⑦봽--updatetick)
9. [?댁젣 ??遺꾨━ ?ㅺ퀎](#9-?댁젣-??遺꾨━-?ㅺ퀎)
10. [肄섑뀗痢??쒕쾭 ?ъ슜 ?⑦꽩](#10-肄섑뀗痢??쒕쾭-?ъ슜-?⑦꽩)
11. [?듭뀡 ?ㅼ젙 (TIMER_TICK_MS)](#11-?듭뀡-?ㅼ젙-timer_tick_ms)

---

## 1. 援ъ“ 媛쒖슂

```
Ticker (?깃???
 ?쒋?? tickInterval: uint32_t          ??TIMER_TICK_MS ?ㅼ젙媛?(ms)
 ?쒋?? tickCount: uint64_t             ???꾩쟻 ???? ?쒋?? nowMs: uint64_t                 ???꾩옱 ?쒓컖 (GetTickCount64())
 ?쒋?? timerEvents: map<TimerEventId, shared_ptr<TimerEvent>>
 ??   ?붴? TimerEventId ???깅줉???대깽?? ?쒋?? unregisterTargetList: vector<TimerEventId>
 ??   ?붴? Fire() 以??댁젣 ?붿껌???대깽??紐⑸줉 (?ㅼ쓬 ?깆뿉 ?쇨큵 ?쒓굅)
 ?쒋?? tickerThread: jthread            ??UpdateTick 猷⑦봽 ?ㅽ뻾
 ?붴?? timerEventsLock: shared_mutex   ???깅줉/?댁젣/議고쉶 蹂댄샇
```

---

## 2. TimerEvent ?대옒??
```cpp
class TimerEvent {
public:
    explicit TimerEvent(TimerEventId id, TimerEventInterval intervalMs)
        : timerEventId(id)
        , interval(intervalMs)
        , nextFireTick(0)
    {}

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
    virtual void Fire() = 0;   // ??肄섑뀗痢??쒕쾭媛 援ы쁽

    friend class Ticker;
    TimerEventId       timerEventId;
    TimerEventInterval interval;
    uint64_t           nextFireTick;
};
```

**????뺤쓽:**

```cpp
using TimerEventId       = uint32_t;   // ?대깽???앸퀎??(肄섑뀗痢??쒕쾭媛 吏곸젒 遺??
using TimerEventInterval = uint32_t;   // 諛쒗솕 媛꾧꺽 (ms)
```

**Fire() ?몄텧 蹂댁옣:**  
`Fire()`????긽 Ticker Thread?먯꽌 ?몄텧?쒕떎.  
利? `Fire()` ?댁뿉???쒕줈 ?ㅻⅨ `TimerEvent`?ㅼ씠 ?숈떆???ㅽ뻾?섏? ?딅뒗??  
?? IO Worker/RecvLogic Worker???怨듭쑀 ?곗씠?곕뒗 蹂꾨룄 ?숆린???꾩슂.

---

## 3. TimerEventCreator ???앹꽦 ?ы띁

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

**TimerEventId ?먮룞 ?좊떦:**  
`nextId`瑜??먯옄?곸쑝濡?利앷??쒖폒 ??긽 怨좎쑀??ID瑜?蹂댁옣?쒕떎.  
肄섑뀗痢??쒕쾭?먯꽌 ?섎룞?쇰줈 ID瑜?愿由ы븷 ?꾩슂媛 ?녿떎.

```cpp
// ?ъ슜 ?덉떆
auto timer = TimerEventCreator::Create<MyGameLoop>(
    16,          // 16ms 媛꾧꺽 (~60fps)
    core,        // MyGameLoop ?앹꽦??異붽? ?뚮씪誘명꽣
    roomManager
);
// timer->GetTimerEventId() == ?먮룞 ?좊떦??ID
Ticker::GetInstance().RegisterTimerEvent(timer);
```

---

## 4. ?⑥닔 ?ㅻ챸

#### `static Ticker& GetInstance()`
- ?꾩뿭 ?깃????몄뒪?댁뒪瑜?諛섑솚?쒕떎.

#### `void Start(unsigned int intervalMs = 16)`
- ticker thread瑜??쒖옉?섍퀬 tick 二쇨린瑜??ㅼ젙?쒕떎.

#### `void Stop()`
- ticker thread瑜?以묐떒?쒕떎.

#### `bool IsRunning() const`
- ticker媛 ?꾩옱 ?숈옉 以묒씤吏 諛섑솚?쒕떎.

#### `uint64_t GetTickCount() const`
- ?꾩쟻 tick count瑜?諛섑솚?쒕떎.

#### `uint64_t GetNowMs() const`
- ticker媛 留덉?留됱쑝濡?湲곕줉???꾩옱 ?쒓컖(ms)??諛섑솚?쒕떎.

#### `bool RegisterTimerEvent(const std::shared_ptr<TimerEvent>& eventObject)`
- timer event瑜??깅줉?쒕떎.

#### `void UnregisterTimerEvent(TimerEventId timerEventId)`
- timer event ?쒓굅瑜??붿껌?쒕떎.
- ?쒗쉶 以?利됱떆 erase 異⑸룎???쇳븯湲??꾪빐 ?대??곸쑝濡?吏???쒓굅?????덈떎.

#### `void UpdateTick()`
- ?대? ticker loop ?⑥닔??

#### `void UnregisterTimerEventImpl()`
- 吏???쒓굅 紐⑸줉???ㅼ젣 ?먮즺援ъ“??諛섏쁺?섎뒗 ?대? ?⑥닔??

---

## 5. Ticker ?깃???
```cpp
class Ticker {
public:
    static Ticker& GetInstance() {
        static Ticker instance;
        return instance;
    }

    void Start(unsigned int intervalMs = 16) {
        tickInterval = intervalMs;
        tickerThread = std::jthread([this] {
            UpdateTick();
        });
    }

    void Stop() {
        tickerThread.request_stop();
    }
};
```

**`MultiSocketRUDPCore::RunAllThreads`?먯꽌:**

```cpp
Ticker::GetInstance().Start(timerTickMs);  // 媛??癒쇱? ?쒖옉
// ...
// StopServer?먯꽌:
Ticker::GetInstance().Stop();              // 媛???섏쨷???뺤?
```

---

## 6. ?대깽???깅줉 ??`RegisterTimerEvent`

```cpp
bool Ticker::RegisterTimerEvent(const std::shared_ptr<TimerEvent>& timerEvent)
{
    std::unique_lock lock(timerEventsLock);
    TimerEventId id = timerEvent->GetTimerEventId();

    if (timerEvents.contains(id)) {
        LOG_ERROR(std::format("TimerEvent {} already registered", id));
        return false;
    }

    // ?ㅼ쓬 諛쒗솕 ?쒓컖 珥덇린??(利됱떆 諛쒗솕 諛⑹?: ?꾩옱 ?쒓컖 + interval)
    timerEvent->SetNextTick(nowMs);

    timerEvents.emplace(id, std::move(timerEvent));
    return true;
}
```

**SetNextTick(nowMs) ?섎?:**

```
nowMs = ?꾩옱 ?쒓컖 (?? 10000ms)
interval = 1000ms

nextFireTick = nowMs + interval = 11000ms
???깅줉 ??1珥???泥?諛쒗솕 (利됱떆 諛쒗솕?섏? ?딆쓬)
```

**thread-safe ?묎렐:**  
`shared_mutex` ?ъ슜?쇰줈 UpdateTick 猷⑦봽(?쎄린)? Register(?곌린)媛 ?덉쟾?섍쾶 怨듭〈.

---

## 7. ?대깽???댁젣 ??`UnregisterTimerEvent`

```cpp
void Ticker::UnregisterTimerEvent(TimerEventId timerEventId)
{
    // ??Fire() ?ㅽ뻾 以??몄텧?????덉쓬 ??利됱떆 map?먯꽌 ?쒓굅?섎㈃ ?댄꽣?덉씠??臾댄슚??    //    ??unregisterTargetList??蹂닿?, ?ㅼ쓬 ???쒖옉 ???쇨큵 ?쒓굅
    {
        std::unique_lock lock(timerEventsLock);
        unregisterTargetList.push_back(timerEventId);
    }
}
```

**利됱떆 ?댁젣媛 ?꾨땶 ?댁쑀:**

```cpp
// UpdateTick ?대?
for (auto& [id, event] : timerEvents) {   // ????猷⑦봽 ?ㅽ뻾 以?    if (event->ShouldFire(nowMs)) {
        event->Fire();
        // Fire() ?댁뿉??UnregisterTimerEvent(id) ?몄텧 媛??
        // ??timerEvents?먯꽌 利됱떆 erase?섎㈃ ?댄꽣?덉씠??臾댄슚????UB
    }
}
```

`unregisterTargetList`??癒쇱? ?볤퀬, ?ㅼ쓬 ?깆쓽 `UpdateTick` ?쒖옉?먯꽌 ?쇨큵 泥섎━.

---

## 8. ?대? 猷⑦봽 ??`UpdateTick`

```cpp
void Ticker::UpdateTick()
{
    while (isRunning.load()) {
        auto frameStart = std::chrono::steady_clock::now();

        // ???꾩옱 ?쒓컖 媛깆떊
        nowMs = GetTickCount64();

        // ???댁젣 ?덉젙 ?대깽???쇨큵 ?쒓굅
        UnregisterTimerEventImpl();
        // ??unique_lock(timerEventsLock)
        // ??for each id in unregisterTargetList: timerEvents.erase(id)
        // ??unregisterTargetList.clear()

        // ??諛쒗솕 泥댄겕
        {
            std::shared_lock lock(timerEventsLock);  // ?쎄린 ?좉툑
            for (auto& [id, event] : timerEvents) {
                if (!event->ShouldFire(nowMs)) continue;

                event->Fire();                        // 肄섑뀗痢?濡쒖쭅 ?ㅽ뻾
                event->SetNextTick(nowMs);            // ?ㅼ쓬 諛쒗솕 ?쒓컖 媛깆떊
            }
        }

        ++tickCount;

        // ???꾨젅???쒓컙 留욎텛湲?(?⑥? ?쒓컙 sleep)
        auto elapsed = std::chrono::steady_clock::now() - frameStart;
        auto remaining = std::chrono::milliseconds(tickInterval) - elapsed;
        if (remaining > std::chrono::milliseconds(0)) {
            std::this_thread::sleep_for(remaining);
        }
    }
}
```

**?뺣????쒓퀎:**

```
tickInterval=16ms (60fps 紐⑺몴):
  Fire() ?ㅽ뻾 ?쒓컙??湲몃㈃ ???ㅼ쓬 ??吏??  Fire() ?ㅽ뻾 ?쒓컙??0?대㈃ ??sleep_for(16ms - 琯) ??16ms

sleep_for ?뺣???
  Windows 湲곕낯: ~15ms ?댁긽??(timeBeginPeriod(1)濡?1ms源뚯? ?μ긽 媛??
  ???뺥솗??60fps瑜?蹂댁옣?섏? ?딆쓬, 寃뚯엫 濡쒖쭅? delta time 湲곕컲?쇰줈 援ы쁽 沅뚯옣
```

---

## 9. ?댁젣 ??遺꾨━ ?ㅺ퀎

```
TimerEvent::Fire() ?ㅽ뻾 以?UnregisterTimerEvent()瑜??몄텧?섎뒗 ?쒕굹由ъ삤:

?? 5珥???1?뚯꽦 ?ㅽ뻾 ?대깽??  void DelayedEvent::Fire() {
      // ?ㅽ뻾...
      Ticker::GetInstance().UnregisterTimerEvent(GetTimerEventId());
      // ???꾩옱 猷⑦봽瑜??쒗쉶 以묒씠誘濡?利됱떆 erase 遺덇?
  }

?닿껐:
  UnregisterTimerEvent() ??unregisterTargetList??push_back
  (?꾩옱 猷⑦봽??怨꾩냽 吏꾪뻾)
  
  ?ㅼ쓬 ???쒖옉 ??UnregisterTimerEventImpl():
  ??for id in unregisterTargetList:
       timerEvents.erase(id)    ??猷⑦봽 諛뽰씠誘濡??덉쟾
```

---

## 10. 肄섑뀗痢??쒕쾭 ?ъ슜 ?⑦꽩

### 諛섎났 ?ㅽ뻾 ?대깽??
```cpp
// GameLoop.h
class GameLoopEvent final : public TimerEvent {
public:
    GameLoopEvent(TimerEventId id, TimerEventInterval interval,
                  MultiSocketRUDPCore& core)
        : TimerEvent(id, interval), core(core) {}

private:
    void Fire() override {
        // 紐⑤뱺 諛⑹쓽 ?곹깭 ?낅뜲?댄듃
        RoomManager::GetInstance().UpdateAll();

        // TPS 濡쒓렇 (二쇨린 議곗젅)
        if (++counter % 60 == 0) {  // 60 ticks = ??1珥?            LOG_DEBUG(std::format("TPS: {}", core.GetTPS()));
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

### 1?뚯꽦 吏???ㅽ뻾 ?대깽??
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
        // 1???ㅽ뻾 ???먭린 ?먯떊 ?댁젣
        Ticker::GetInstance().UnregisterTimerEvent(GetTimerEventId());
    }

    SessionIdType targetId;
    MultiSocketRUDPCore& core;
};

// ?ъ슜
void Player::OnConnected() {
    auto evt = TimerEventCreator::Create<DelayedPacketEvent>(
        5000,          // 5珥???        GetSessionId(),
        core
    );
    Ticker::GetInstance().RegisterTimerEvent(evt);
}
```

### 二쇨린??泥?냼 ?대깽??
```cpp
class CleanupEvent final : public TimerEvent {
public:
    CleanupEvent(TimerEventId id, MultiSocketRUDPCore& core)
        : TimerEvent(id, 30000), core(core) {}  // 30珥덈쭏??
private:
    void Fire() override {
        // 鍮꾪솢??猷??뺣━
        RoomManager::GetInstance().CleanupEmptyRooms();
    }
    MultiSocketRUDPCore& core;
};
```

---

## 11. ?듭뀡 ?ㅼ젙 (TIMER_TICK_MS)

```ini
; CoreOption.ini
TIMER_TICK_MS=16    ; 16ms 媛꾧꺽 (~60fps)
```

**沅뚯옣媛?**

| ?⑸룄 | TIMER_TICK_MS | ?댁쑀 |
|------|---------------|------|
| 60fps 寃뚯엫 猷⑦봽 | 16 | 1000/60 ??16.7ms |
| ?쒕쾭 痢??곹깭 愿由?| 50 | 20fps, CPU ?덉빟 |
| 諛곗튂 泥섎━ | 100~1000 | ??? 鍮덈룄, ?뺣???遺덊븘??|
| ?섑듃鍮꾪듃 泥댄겕 | 1000 | 1珥?二쇨린 |

> `Fire()` ???묒뾽??`TIMER_TICK_MS`蹂대떎 ?ㅻ옒 嫄몃━硫??ㅼ쓬 ?깆씠 吏?곕맂??  
> 臾닿굅???묒뾽? 蹂꾨룄 ?ㅻ젅?쒖뿉 ?꾩엫?섍퀬 `Fire()`?먯꽌???묒뾽 ?먯뿉 異붽?留??쒕떎.

---

## 愿??臾몄꽌
- [[MultiSocketRUDPCore]] ??`Ticker::Start(timerTickMs)` ?몄텧
- [[ContentServerGuide]] ??肄섑뀗痢??쒕쾭?먯꽌 Ticker ?쒖슜 ?덉떆
- [[ThreadModel]] ??Ticker Thread 醫낅즺 ?쒖꽌
