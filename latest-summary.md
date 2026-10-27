<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `0cfee60e6729`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 184.695 µs | 227.600 µs | +4.64% | 253.400 µs | -2.39% | 438.400 µs |
| TX/RX Loss 10% | 1,000 × 5 | 6.053 ms | 32.147 ms | -0.88% | 64.236 ms | -2.50% | 265.126 ms |

Measured at (UTC): `2026-10-27T18:53:13.4989717+00:00`  
Commit log: * 원격 라이브러리 수정과 외부 접속·송신 큐 계측 변경 통합

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
