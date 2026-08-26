<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `61833d0a2d18`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 177.419 µs | 225.300 µs | +4.21% | 259.300 µs | +7.15% | 5.212 ms |
| TX/RX Loss 10% | 1,000 × 5 | 6.126 ms | 32.228 ms | -0.55% | 64.554 ms | +0.64% | 502.163 ms |

Measured at (UTC): `2026-08-26T10:53:51.4065138+00:00`  
Commit log: * 주석 없던 테스트들에 테스트 목적 주석 추가

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
