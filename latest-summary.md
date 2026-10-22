<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `fa51ffb56460`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 132.969 µs | 166.400 µs | -2.58% | 187.600 µs | -7.31% | 1.435 ms |
| TX/RX Loss 10% | 1,000 × 5 | 6.140 ms | 32.619 ms | -0.02% | 64.520 ms | +0.52% | 297.438 ms |

Measured at (UTC): `2026-10-22T06:50:44.7752157+00:00`  
Commit log: * NetBuffer 지연 초기화 연동과 ASan 회귀 검증 보완

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
