<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `ac94f2c0d029`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 165.713 µs | 235.900 µs | +7.03% | 308.300 µs | +21.23% | 4.218 ms |
| TX/RX Loss 10% | 1,000 × 5 | 6.332 ms | 32.494 ms | +0.15% | 64.719 ms | +0.31% | 265.106 ms |

Measured at (UTC): `2026-11-15T07:04:32.5611748+00:00`  
Commit log: * 원격 라이브러리 변경과 진단 로그 개선 통합

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
