<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `6de51f6ed5de`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 115.993 µs | 149.100 µs | -31.26% | 177.400 µs | -26.82% | 1.550 ms |
| TX/RX Loss 10% | 1,000 × 5 | 5.833 ms | 32.240 ms | -0.69% | 64.446 ms | +0.10% | 201.116 ms |

Measured at (UTC): `2026-09-09T15:50:18.563934+00:00`  
Commit log: * TLS 교환 진행할 때 TCP를 연결하고 아무 행동도 하지 않을 시 접속 마비가 가능한 현상 수정

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
