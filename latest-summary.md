<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `6a05d49c6660`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 99.872 µs | 140.300 µs | -42.14% | 160.000 µs | -40.98% | 291.300 µs |
| TX/RX Loss 10% | 1,000 × 5 | 5.855 ms | 32.160 ms | -0.34% | 66.918 ms | +5.33% | 265.279 ms |

Measured at (UTC): `2026-11-11T05:56:42.2565768+00:00`  
Commit log: * CI 테스트 인증서 등록 대기 문제 수정

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
