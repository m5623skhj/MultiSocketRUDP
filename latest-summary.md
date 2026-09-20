<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `7a88abee008b`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 109.846 µs | 135.000 µs | -44.94% | 162.800 µs | -44.81% | 10.111 ms |
| TX/RX Loss 10% | 1,000 × 5 | 5.716 ms | 32.626 ms | -2.37% | 64.322 ms | -0.01% | 297.190 ms |

Measured at (UTC): `2026-09-20T04:09:00.7499022+00:00`  
Commit log: * 클라이언트 패킷 페이로드 검증 오류 수정

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
