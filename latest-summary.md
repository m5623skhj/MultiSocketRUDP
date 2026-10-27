<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `ce5083e1732e`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 182.226 µs | 217.500 µs | +54.26% | 259.600 µs | +64.20% | 1.863 ms |
| TX/RX Loss 10% | 1,000 × 5 | 5.673 ms | 32.431 ms | -0.20% | 65.886 ms | +2.53% | 301.137 ms |

Measured at (UTC): `2026-10-27T18:47:00.6438618+00:00`  
Commit log: * 호스트 로컬 RUDP 세션 접속 옵션 지원

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
