<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `6a8a448713f6`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 179.815 µs | 219.000 µs | +0.50% | 251.400 µs | +3.33% | 927.100 µs |
| TX/RX Loss 10% | 1,000 × 5 | 5.759 ms | 31.927 ms | -1.46% | 64.570 ms | -6.77% | 200.667 ms |

Measured at (UTC): `2026-09-08T07:09:09.3461463+00:00`  
Commit log: * 빌드 툴 버전 업

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
