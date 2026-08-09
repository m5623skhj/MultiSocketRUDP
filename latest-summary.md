<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `cabf79193e81`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 158.510 µs | 206.600 µs | -6.47% | 237.500 µs | -5.75% | 1.575 ms |
| TX/RX Loss 10% | 1,000 × 5 | 5.889 ms | 32.469 ms | +0.23% | 63.264 ms | +0.10% | 200.662 ms |

Measured at (UTC): `2026-08-09T07:55:10.1873707+00:00`  
Commit log: * 재전송 Wake 이벤트 신호 실패 처리를 수정   * NULL 핸들을 성공으로 처리하던 예외 제거   * SetEvent 결과를 직접 반환해 오류 코드 보존

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
