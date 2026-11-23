<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `254ebba42718`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 41.231 µs | 65.300 µs | -71.27% | 83.600 µs | -66.51% | 1.537 ms |
| TX/RX Loss 10% | 1,000 × 5 | 5.725 ms | 32.581 ms | +0.37% | 63.520 ms | -0.72% | 403.163 ms |

Measured at (UTC): `2026-11-23T15:06:14.5483421+00:00`  
Commit log: * RUDP 서버·클라이언트 진단 통합

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
