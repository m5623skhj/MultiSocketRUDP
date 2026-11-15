<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `3b21e76e67ea`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 161.013 µs | 212.900 µs | -9.75% | 242.200 µs | -21.44% | 3.968 ms |
| TX/RX Loss 10% | 1,000 × 5 | 5.969 ms | 32.449 ms | -0.14% | 64.507 ms | -0.33% | 196.761 ms |

Measured at (UTC): `2026-11-15T23:30:53.3641386+00:00`  
Commit log: * 통합 클라이언트의 Windows 소켓 헤더 포함 순서 수정

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
