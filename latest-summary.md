<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `611752b72290`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 167.157 µs | 217.300 µs | -3.55% | 238.000 µs | -8.21% | 853.200 µs |
| TX/RX Loss 10% | 1,000 × 5 | 5.926 ms | 32.342 ms | +0.35% | 66.364 ms | +2.80% | 213.440 ms |

Measured at (UTC): `2026-08-26T11:59:45.8524075+00:00`  
Commit log: * 주석 최신화 * 클라이언트 수신 패킷 시퀀스 동기화 수정

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
