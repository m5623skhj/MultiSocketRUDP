<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `69aa11ba940c`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 185.357 µs | 220.400 µs | +3.91% | 254.300 µs | -6.85% | 1.302 ms |
| TX/RX Loss 10% | 1,000 × 5 | 5.789 ms | 32.445 ms | -0.68% | 64.516 ms | -0.13% | 200.539 ms |

Measured at (UTC): `2026-11-11T06:40:46.0005541+00:00`  
Commit log: * README의 비신뢰성 RTT 테스트 그래프 단위 수정

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
