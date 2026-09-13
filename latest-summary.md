<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `0816aa0654e9`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 168.220 µs | 208.800 µs | -6.74% | 228.800 µs | -6.00% | 1.457 ms |
| TX/RX Loss 10% | 1,000 × 5 | 6.198 ms | 33.397 ms | +3.74% | 65.643 ms | +2.06% | 201.147 ms |

Measured at (UTC): `2026-09-13T13:36:47.2979577+00:00`  
Commit log: * 비신뢰성·혼합 채널 RTT 성능 측정 CI 추가   * 요청 ID 기반 에코 패킷으로 신뢰성·비신뢰성 왕복 측정   * 비신뢰성 단독, 신뢰성 기준 및 혼합 전송 시나리오 추가   * 채널별 RTT p50·p95·p99, 응답률 및 처리량 기록     * 미수신·지연 응답을 RTT 표본과 구분하여 집계   * 이전 커밋 대비 성능 비교와 결과 이력 저장     * PR 요약, Job Summary 및 artifact에 결과 출력     * 지연 변화는 보고하고 응답 불능·연결 종료 등은 실패 처리   * 집계·이력 비교 테스트와 측정 문서 추가

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
