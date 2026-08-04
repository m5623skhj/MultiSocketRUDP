<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `34b8102718e7`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 117.451 µs | 162.200 µs | -11.56% | 192.200 µs | -3.95% | 5.876 ms |
| TX/RX Loss 10% | 1,000 × 5 | 6.052 ms | 32.416 ms | +0.05% | 62.135 ms | -2.89% | 501.676 ms |

Measured at (UTC): `2026-08-04T11:02:11.1842539+00:00`  
Commit log: * README RTT 표에서 현재 RTT를 알 수 있도록 추가

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
