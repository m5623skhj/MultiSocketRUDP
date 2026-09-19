<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `38c37e896ade`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 182.707 µs | 245.200 µs | +7.03% | 295.000 µs | +19.97% | 6.046 ms |
| TX/RX Loss 10% | 1,000 × 5 | 6.336 ms | 33.419 ms | +4.81% | 64.328 ms | +0.63% | 501.936 ms |

Measured at (UTC): `2026-09-19T23:13:51.5792932+00:00`  
Commit log: * 패킷 생성기에서 사용자 정의 구조체를 정의하고, 이를 패킷에서 사용할 수 있도록 수정

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
