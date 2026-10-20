<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `caf723f66adc`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 183.157 µs | 234.300 µs | +3.67% | 252.200 µs | +2.73% | 721.600 µs |
| TX/RX Loss 10% | 1,000 × 5 | 6.099 ms | 32.610 ms | +2.11% | 64.612 ms | +1.27% | 265.182 ms |

Measured at (UTC): `2026-10-20T05:16:17.4722912+00:00`  
Commit log: * 클라이언트 응답 훅 경고 및 공통 코드 참조 정리

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
