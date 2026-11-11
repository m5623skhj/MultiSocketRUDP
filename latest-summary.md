<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `b5fed3cd313d`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 156.623 µs | 212.100 µs | +51.18% | 273.000 µs | +70.62% | 5.951 ms |
| TX/RX Loss 10% | 1,000 × 5 | 6.181 ms | 32.665 ms | +1.57% | 64.601 ms | -3.46% | 350.829 ms |

Measured at (UTC): `2026-11-11T06:33:11.0999501+00:00`  
Commit log: * RTT 벤치마크 테스트를 일관성 있게 그래프가 보이도록 수정   * 비신뢰성 송신 테스트를 10회 기준에서 1회 기준으로 수정   * 비신뢰성 송신 테스트를 10ms 대기에서 수신 즉시 재송신으로 수정

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
