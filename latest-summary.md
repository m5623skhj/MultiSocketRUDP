<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `f72bf31f3165`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 163.928 µs | 216.200 µs | -2.57% | 242.000 µs | -2.69% | 1.686 ms |
| TX/RX Loss 10% | 1,000 × 5 | 5.788 ms | 32.405 ms | +0.20% | 64.146 ms | -0.03% | 213.380 ms |

Measured at (UTC): `2026-08-26T10:48:10.6791112+00:00`  
Commit log: * 인코딩 잘못된 주석 수정

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
