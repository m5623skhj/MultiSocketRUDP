<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `b8ed6ca62eb2`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 166.695 µs | 209.400 µs | -2.74% | 237.600 µs | -1.16% | 1.940 ms |
| TX/RX Loss 10% | 1,000 × 5 | 6.316 ms | 32.671 ms | +0.65% | 64.351 ms | +0.51% | 365.413 ms |

Measured at (UTC): `2026-09-09T04:13:42.9215035+00:00`  
Commit log: * 예약 세션 종료 상태를 분리하여 접속자 수와 종료 콜백 처리 수정   * RESERVED에서 종료 시 RELEASING_BY_ABORT_RESERVED 상태로 전환   * 두 종료 상태를 공통 해제 경로에서 처리   * 실제 연결됐던 세션만 접속자 수 감소 및 종료 콜백 수행   * 예약 종료·세션 재사용·중복 해제·연결과 종료 경합 테스트 추가   * 기존 상태 전이 테스트와 해제 상태 설정 헬퍼 갱신

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
