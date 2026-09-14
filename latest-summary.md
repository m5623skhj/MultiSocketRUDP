<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `6ca5c163b9b8`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 101.358 µs | 133.500 µs | -42.28% | 150.000 µs | -45.45% | 475.700 µs |
| TX/RX Loss 10% | 1,000 × 5 | 5.876 ms | 32.360 ms | -1.66% | 63.627 ms | -1.92% | 297.168 ms |

Measured at (UTC): `2026-09-14T01:48:16.4962883+00:00`  
Commit log: * 비신뢰성 채널 RTT·응답률 추이 그래프 및 README 연동   * 단독·혼합 전송별 P95/P99 RTT와 응답률, 커밋별 변화율 표시   * 동일 측정 조건의 최근 10회 선택 및 응답 없는 RTT 구간 처리   * 공식 벤치마크 CI에서 SVG 자동 생성·저장   * README 및 벤치마크 문서 갱신   * 이력 필터링과 누락 응답 처리 회귀 테스트 추가

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
