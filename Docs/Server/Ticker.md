# Ticker

> 서버 전역 주기 이벤트 실행기다. `TimerEvent`를 등록해 주기 작업을 돌린다.

---

## 기본 사용법

```cpp
class GameLoopEvent final : public TimerEvent
{
public:
    GameLoopEvent(TimerEventId inId, TimerEventInterval inInterval)
        : TimerEvent(inId, inInterval)
    {
    }

private:
    void Fire() override
    {
        // 게임 루프 작업
    }
};

auto loop = TimerEventCreator::Create<GameLoopEvent>(16);
Ticker::GetInstance().RegisterTimerEvent(loop);
```

`MultiSocketRUDPCore`는 시작 과정에서 `Ticker::GetInstance().Start(timerTickMs)`를 호출한다.

---

## 공개 API 주의사항

### `core.GetUsingSession(...)` 예제 제거

현재 `MultiSocketRUDPCore::GetUsingSession()`은 `private`이다.  
따라서 아래 패턴을 콘텐츠 코드 예제로 두면 안 된다.

```cpp
auto* session = core.GetUsingSession(targetId); // 현재 외부 호출 불가
```

Ticker 문서에서는 세션 조회를 공개 API처럼 설명하지 않는다.

### 권장 대체

Ticker에서는 아래처럼 "자체 매니저나 서비스 계층"을 호출하는 패턴으로 설명하는 편이 맞다.

```cpp
void Fire() override
{
    RoomManager::GetInstance().UpdateAll();
}
```

---

## one-shot 이벤트

한 번만 실행하는 타이머는 `Fire()` 내부에서 자기 자신을 해제하면 된다.

```cpp
void Fire() override
{
    // 작업 수행
    Ticker::GetInstance().UnregisterTimerEvent(GetTimerEventId());
}
```

---

## 주의할 점

- `Fire()`는 Ticker thread에서 실행된다.
- 무거운 블로킹 작업은 피한다.
- Ticker 문서에서는 현재 공개되지 않은 코어 세션 조회 API를 사용 예제로 노출하지 않는다.

---

## 관련 문서

- [[MultiSocketRUDPCore]] - Ticker 시작 지점
- [[ContentServerGuide]] - 콘텐츠 서버 구성
