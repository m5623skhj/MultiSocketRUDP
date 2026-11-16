<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `8b65e4b98e78`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 98.974 µs | 137.300 µs | -35.51% | 156.300 µs | -35.47% | 416.800 µs |
| TX/RX Loss 10% | 1,000 × 5 | 6.214 ms | 32.461 ms | +0.04% | 65.435 ms | +1.44% | 243.581 ms |

Measured at (UTC): `2026-11-16T15:27:07.4254879+00:00`  
Commit log: * 서버와 클라이언트 TLS 변경 이력 통합

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
