<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `f75b7248d10a`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 150.134 µs | 183.400 µs | -10.36% | 200.100 µs | -19.35% | 1.893 ms |
| TX/RX Loss 10% | 1,000 × 5 | 5.873 ms | 32.400 ms | -0.63% | 63.983 ms | -0.50% | 264.875 ms |

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
