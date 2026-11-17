<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `d39f3561c85e`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 188.341 µs | 227.300 µs | +65.55% | 249.600 µs | +59.69% | 1.660 ms |
| TX/RX Loss 10% | 1,000 × 5 | 5.972 ms | 32.461 ms | +0.00% | 63.982 ms | -2.22% | 301.700 ms |

Measured at (UTC): `2026-11-17T18:46:19.3412584+00:00`  
Commit log: * TLS·브로커 연결 단계별 진단과 오류 원인 보존

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
