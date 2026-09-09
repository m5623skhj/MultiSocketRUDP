<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `dd9dfaf3428e`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 184.024 µs | 215.300 µs | +19.61% | 240.400 µs | +22.22% | 4.219 ms |
| TX/RX Loss 10% | 1,000 × 5 | 5.909 ms | 32.459 ms | +0.08% | 64.027 ms | -0.96% | 300.956 ms |

Measured at (UTC): `2026-09-09T03:59:06.6276371+00:00`  
Commit log: * 실패한 테스트 수정

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
