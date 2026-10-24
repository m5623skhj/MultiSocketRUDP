<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `bb5956829f6a`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 110.396 µs | 141.000 µs | -15.26% | 158.100 µs | -15.72% | 3.433 ms |
| TX/RX Loss 10% | 1,000 × 5 | 5.850 ms | 32.496 ms | -0.38% | 64.258 ms | -0.41% | 200.765 ms |

Measured at (UTC): `2026-10-24T01:38:37.5627641+00:00`  
Commit log: * 원격 RUDP 버퍼 및 ACK 종료 경합 수정 통합

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
