<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `fc496f9e4c58`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 100.508 µs | 135.400 µs | -16.47% | 155.900 µs | -22.55% | 1.537 ms |
| TX/RX Loss 10% | 1,000 × 5 | 6.061 ms | 32.380 ms | +1.01% | 64.606 ms | +1.35% | 264.882 ms |

Measured at (UTC): `2026-10-01T04:57:57.6415824+00:00`  
Commit log: * 서버 종료 시 대기 중인 브로커 소켓 정리 지연 개선   * 전체 워커 중지 요청 후 큐 소켓을 먼저 닫고 워커 종료 대기   * 종료 테스트 실패 메시지에 소켓 인덱스와 실제 대기 시간 추가   * Debug 빌드 및 종료 테스트 5회·관련 TLS 테스트 2개 통과

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
