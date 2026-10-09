<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `622f8a081c0a`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 182.008 µs | 226.000 µs | +0.13% | 245.500 µs | -0.04% | 1.494 ms |
| TX/RX Loss 10% | 1,000 × 5 | 6.022 ms | 31.936 ms | -1.46% | 63.801 ms | -1.47% | 258.423 ms |

Measured at (UTC): `2026-10-09T15:03:17.5112965+00:00`  
Commit log: * 코드 정리

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
