<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `5a2b89084cd4`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 96.876 µs | 133.800 µs | -19.88% | 147.600 µs | -29.28% | 1.579 ms |
| TX/RX Loss 10% | 1,000 × 5 | 6.320 ms | 32.375 ms | -0.18% | 68.001 ms | +5.83% | 301.379 ms |

Measured at (UTC): `2026-09-11T15:25:18.0106004+00:00`  
Commit log: * ACK로 취소된 재전송 패킷이 송신 길이에 포함되는 문제 수정   * 패킷 폐기 검사 통과 후 totalSendSize를 증가하도록 순서 변경   * 취소된 패킷으로 인한 후속 복사 위치 오류와 미기록 버퍼 영역 송신 방지

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
