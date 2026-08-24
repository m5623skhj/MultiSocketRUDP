<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `640064d22530`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 151.124 µs | 221.900 µs | +7.41% | 248.700 µs | +4.72% | 1.822 ms |
| TX/RX Loss 10% | 1,000 × 5 | 5.923 ms | 32.340 ms | -0.40% | 64.168 ms | +1.43% | 366.848 ms |

Measured at (UTC): `2026-08-24T12:23:09.4406437+00:00`  
Commit log: * Schannel TLS 핸드셰이크 설정 및 실패 진단 개선

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
