<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `530e610cefbe`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 196.969 µs | 229.100 µs | +71.61% | 245.900 µs | +63.93% | 15.489 ms |
| TX/RX Loss 10% | 1,000 × 5 | 5.684 ms | 31.884 ms | -1.47% | 63.922 ms | +0.46% | 164.662 ms |

Measured at (UTC): `2026-09-19T17:53:30.6810694+00:00`  
Commit log: * 버퍼 컨테이너 추가로 인한 연관 테스트 추가

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
