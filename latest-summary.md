<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `f3da8b1550db`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 115.140 µs | 156.900 µs | -31.06% | 180.200 µs | -28.89% | 587.700 µs |
| TX/RX Loss 10% | 1,000 × 5 | 5.921 ms | 32.290 ms | +0.45% | 64.549 ms | +0.49% | 401.702 ms |

Measured at (UTC): `2026-10-28T00:08:04.2742878+00:00`  
Commit log: * 문서 전면 최신화 및 코드 대조 오류 정정

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
