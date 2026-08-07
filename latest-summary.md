<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `536de210ebfd`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 132.376 µs | 179.700 µs | +10.79% | 205.400 µs | +6.87% | 5.130 ms |
| TX/RX Loss 10% | 1,000 × 5 | 5.914 ms | 32.289 ms | -0.39% | 64.362 ms | +3.58% | 401.581 ms |

Measured at (UTC): `2026-08-07T05:50:45.4882624+00:00`  
Commit log: * 코드 정리 * 클라이언트 TLS 보안 강화

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
