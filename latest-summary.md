<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `0b8ab0de168f`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 142.269 µs | 170.800 µs | -27.10% | 202.400 µs | -19.75% | 1.395 ms |
| TX/RX Loss 10% | 1,000 × 5 | 5.920 ms | 32.627 ms | +0.05% | 64.184 ms | -0.66% | 465.537 ms |

Measured at (UTC): `2026-10-21T16:47:45.8366079+00:00`  
Commit log: * RUDP 진단 로그 조건문 범위와 소스 인코딩 수정

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
