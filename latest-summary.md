<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `03293c8f5918`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 161.961 µs | 204.600 µs | -99.67% | 248.100 µs | -99.61% | 5.476 ms |
| TX/RX Loss 10% | 1,000 × 5 | 6.372 ms | 32.606 ms | -68.16% | 64.302 ms | -58.16% | 301.024 ms |

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
