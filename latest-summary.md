<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `cb4ed7d09e43`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 175.474 µs | 220.900 µs | +22.93% | 252.000 µs | +22.69% | 947.600 µs |
| TX/RX Loss 10% | 1,000 × 5 | 5.948 ms | 32.396 ms | +0.33% | 63.201 ms | -1.81% | 297.058 ms |

Measured at (UTC): `2026-08-07T15:22:38.1441748+00:00`  
Commit log: * 주석 수정

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
