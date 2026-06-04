# 성능 튜닝 가이드

> 현재 코드 기준으로 바로 조정 가능한 성능 관련 항목만 정리한다.

---

## 스레드와 슬립 설정

### `THREAD_COUNT`

`THREAD_COUNT`는 IO worker, recv logic worker, retransmission worker 분산 폭에 직접 영향이 있다.  
과도하게 키우면 컨텍스트 스위칭 비용이 커지고, 너무 작으면 특정 worker에 세션이 몰린다.

### `WORKER_THREAD_ONE_FRAME_MS`

옵션 파일과 함께 `BuildConfig.h` 매크로도 같이 봐야 한다.

현재 `BuildConfig.h`에는 아래 상수가 있다.

```cpp
#define NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME 0
#define USE_WORKER_THREAD_SLEEP_FOR_FRAME 1
#define USE_WORKER_THREAD_SLEEP_ZERO 2

#define USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME USE_WORKER_THREAD_SLEEP_ZERO
```

즉 이 값은 단순 0/1 스위치가 아니라 3단계 모드다.

---

## 재전송 관련

- `RETRANSMISSION_MS`
- `RETRANSMISSION_THREAD_SLEEP_MS`
- `MAX_PACKET_RETRANSMISSION_COUNT`

이 세 값은 함께 튜닝해야 한다.  
너무 공격적으로 줄이면 정상 지연도 끊김으로 판단할 수 있고, 너무 늘리면 실패 감지가 느려진다.

---

## 흐름 제어 관련

- holding queue 크기
- CWND 상수
- worker 병목 여부

이 셋이 실제 처리량에 같이 영향을 준다.

특히 핸들러가 오래 블로킹되면 advertise window가 줄어들고, 결국 전송 측 throughput도 떨어진다.

---

## 통계 확인

현재 코어에는 아래 조회 함수가 이미 있다.

```cpp
GetTPS()
ResetTPS()
GetNowSessionCount()
GetUnusedSessionCount()
```

예전 문서처럼 `GetUnusedSessionCount()`를 "추가 구현 필요"로 보면 안 된다.

---

## 운영 팁

- 로컬 테스트에서는 먼저 `THREAD_COUNT`, `RETRANSMISSION_MS`, `MAX_HOLDING_PACKET_QUEUE_SIZE`만 조정해도 차이가 크다.
- 핸들러 내부 블로킹 작업은 성능 튜닝보다 먼저 제거하는 것이 맞다.
- 튜닝 전후에는 `GetTPS()`와 연결 수 추이를 같이 본다.

---

## 관련 문서

- [[FlowController]] - 흐름 제어 개념
- [[MultiSocketRUDPCore]] - 통계 조회 API
- [[Troubleshooting]] - 성능 이슈 점검
