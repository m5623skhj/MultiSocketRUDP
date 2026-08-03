<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `cb43718ce26c`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 43.710 ms | 62.839 ms | - | 63.596 ms | - | 136.183 ms |
| TX/RX Loss 10% | 1,000 × 5 | 49.234 ms | 102.392 ms | - | 153.679 ms | - | 406.707 ms |

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
