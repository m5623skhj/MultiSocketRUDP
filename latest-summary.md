<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `546fc8dc936e`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 136.549 µs | 180.000 µs | +19.28% | 196.700 µs | +13.63% | 463.700 µs |
| TX/RX Loss 10% | 1,000 × 5 | 6.114 ms | 32.433 ms | +0.47% | 64.645 ms | +1.83% | 201.053 ms |

Measured at (UTC): `2026-09-08T15:46:49.1420719+00:00`  
Commit log: * 이전 세션의 재전송 작업이 재사용된 세션에 영향을 주는 경쟁 조건 수정   * generation 검사와 송신 작업 등록을 동일한 mutex로 보호   * 재전송 처리와 실패 정리가 완료될 때까지 세션 해제 지연   * 이전 generation 및 종료 중인 세션의 재전송 작업 거절   * 세션 재사용과 재전송 처리 중 해제 경쟁 회귀 테스트 추가   * 재전송 작업의 세션 수명 보호 문서 갱신

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
