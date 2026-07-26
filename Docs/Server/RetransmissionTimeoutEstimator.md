# RetransmissionTimeoutEstimator

> **RTT 표본으로 서버 세션의 재전송 timeout(RTO)을 추정한다.**  
> RFC 6298의 SRTT/RTTVAR 계산식을 정수 밀리초 단위로 적용하고, timeout 발생 시 지수 backoff를 수행한다.

---

## 목차

1. [초기화](#1-초기화)
2. [현재 RTO 조회](#2-현재-rto-조회)
3. [RTT 표본 반영](#3-rtt-표본-반영)
4. [Timeout backoff](#4-timeout-backoff)
5. [동시성 보호](#5-동시성-보호)

---

## 1. 초기화

### `Configure`

```cpp
void Configure(
    unsigned int inInitialRtoMs,
    unsigned int inMinRtoMs,
    unsigned int inMaxRtoMs);
```

RTO 범위를 설정하고 이전 RTT 측정 상태와 backoff 억제 구간을 초기화한다.

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| `inInitialRtoMs` | `unsigned int` | 유효한 RTT 표본을 얻기 전에 사용할 초기 RTO |
| `inMinRtoMs` | `unsigned int` | 계산된 RTO의 하한. `1ms`보다 작으면 `1ms`로 보정한다. |
| `inMaxRtoMs` | `unsigned int` | 계산 및 backoff RTO의 상한. 하한보다 작으면 하한으로 보정한다. |

초기 RTO도 보정된 최소/최대 범위 안으로 제한한다.

---

## 2. 현재 RTO 조회

### `GetRtoMs`

```cpp
[[nodiscard]]
unsigned int GetRtoMs() const noexcept;
```

현재 캐시된 RTO를 밀리초 단위로 반환한다.

> 반환값을 무시하면 컴파일 경고가 발생한다. 호출 측에서 반드시 검사해야 한다.

---

## 3. RTT 표본 반영

### `OnRttSample`

```cpp
void OnRttSample(std::chrono::steady_clock::duration sample);
```

재전송되지 않은 패킷에서 얻은 RTT 표본을 반영한다. 밀리초 변환 결과가 `1ms` 이하이면 `1ms`로 사용한다.

첫 표본은 아래처럼 초기화한다.

```text
SRTT   = sample
RTTVAR = sample / 2
```

이후 표본은 부동소수점 연산 없이 다음 가중치를 적용한다.

```text
RTTVAR = (3 × RTTVAR + |SRTT - sample|) / 4
SRTT   = (7 × SRTT + sample) / 8
RTO    = SRTT + max(1ms, 4 × RTTVAR)
```

계산한 RTO는 설정 범위로 제한하며, 새 표본을 반영하면 이전 timeout backoff 억제 구간을 해제한다.

---

## 4. Timeout backoff

### `OnTimeout`

```cpp
[[nodiscard]]
bool OnTimeout(std::chrono::steady_clock::time_point now);
```

현재 RTO를 두 배로 늘리되 설정된 최대값을 넘지 않도록 제한한다. 같은 timeout window에서 여러 패킷이 연속 만료되어도 backoff는 한 번만 적용한다.

| 반환값 | 조건 |
|--------|------|
| `true` | 새 backoff를 적용하고 억제 종료 시각을 갱신함 |
| `false` | 현재 시각이 기존 억제 구간 안이라 backoff를 생략함 |

> 반환값을 무시하면 컴파일 경고가 발생한다. 호출 측에서 반드시 검사해야 한다.

`RUDPSession::OnRetransmissionTimeout()`은 `true`일 때만 흐름 제어기의 timeout 처리를 함께 호출한다.

---

## 5. 동시성 보호

- `lock`은 RTT 상태, 범위, backoff 억제 종료 시각을 보호한다.
- `cachedRtoMs`는 `std::atomic_uint`이므로 `GetRtoMs()`는 mutex를 획득하지 않는다.
- `Configure()`, `OnRttSample()`, `OnTimeout()`은 같은 mutex로 직렬화된다.

---

## 관련 문서

- [[RUDPSession]] - estimator 소유 및 RTT/timeout 전달
- [[SendPacketInfo]] - 유효한 RTT 표본 판정
- [[ThreadModel]] - 재전송 scheduler와 worker 흐름
- [[PerformanceTuning]] - 초기/최소/최대 RTO 설정
