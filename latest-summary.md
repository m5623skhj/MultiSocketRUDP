<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `37cc2df9d603`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 186.392 µs | 225.700 µs | +0.67% | 245.600 µs | -2.54% | 1.507 ms |
| TX/RX Loss 10% | 1,000 × 5 | 5.958 ms | 32.411 ms | -1.29% | 64.753 ms | -3.77% | 200.693 ms |

Measured at (UTC): `2026-10-04T15:45:32.0674409+00:00`  
Commit log: * 클라이언트 코어 종료 시 연결·송수신 대기와 로그 스레드 정리 개선

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
