<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `cccba6b679b1`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 113.335 µs | 150.900 µs | -31.10% | 173.100 µs | -31.15% | 434.300 µs |
| TX/RX Loss 10% | 1,000 × 5 | 5.381 ms | 32.280 ms | +1.10% | 63.482 ms | -1.68% | 200.934 ms |

Measured at (UTC): `2026-09-08T12:50:36.2189927+00:00`  
Commit log: * 송신 준비가 끝나기 전에 세션의 암호화 자원을 해제 문제 수정

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
