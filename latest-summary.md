<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `4f0dfe9b0d54`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 189.891 µs | 228.500 µs | +45.63% | 261.000 µs | +44.84% | 1.523 ms |
| TX/RX Loss 10% | 1,000 × 5 | 5.965 ms | 32.307 ms | +0.05% | 63.024 ms | -2.36% | 595.994 ms |

Measured at (UTC): `2026-11-06T00:28:17.1679688+00:00`  
Commit log: * 통합 테스트 CI의 TLS 인증서 신뢰 설정 수정

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
