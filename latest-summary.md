<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `99db792203b8`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 206.413 µs | 242.500 µs | +6.13% | 271.100 µs | +3.87% | 1.648 ms |
| TX/RX Loss 10% | 1,000 × 5 | 5.706 ms | 32.271 ms | -0.11% | 63.531 ms | +0.81% | 224.446 ms |

Measured at (UTC): `2026-11-06T09:52:52.0660948+00:00`  
Commit log: * 던전 비신뢰 패킷의 역순 수신과 재생 방지 지원

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
