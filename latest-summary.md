<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `c73035c45955`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 177.932 µs | 216.900 µs | +3.58% | 242.400 µs | +2.02% | 1.807 ms |
| TX/RX Loss 10% | 1,000 × 5 | 5.979 ms | 32.464 ms | -0.63% | 64.381 ms | +0.05% | 296.946 ms |

Measured at (UTC): `2026-09-09T15:32:44.1339381+00:00`  
Commit log: * 송신 흐름 제어 상태의 동시 접근 동기화   * ACK·타임아웃 갱신과 송신 가능 여부 조회를 동일한 mutex로 보호   * 혼잡 윈도우 조회와 송신 상태 초기화에 동일한 동기화 적용   * ACK와 타임아웃 경합 결과의 순차 실행 일관성 테스트 추가   * 송신 판단이 불완전한 ACK·윈도우 조합을 관찰하지 않는지 검증

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
