<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `5201fe3b0176`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 173.951 µs | 223.900 µs | +67.34% | 243.400 µs | +64.91% | 1.874 ms |
| TX/RX Loss 10% | 1,000 × 5 | 5.567 ms | 32.194 ms | -0.56% | 64.317 ms | -5.42% | 197.246 ms |

Measured at (UTC): `2026-09-13T12:45:10.5018743+00:00`  
Commit log: * 연결 ACK 유실 복구 및 중복 연결 처리 방지   * 서버가 동일 클라이언트의 연결 재요청에 ACK만 재전송하도록 수정     * 접속자 수, 연결 콜백 및 수신 상태 유지     * 다른 주소·포트와 종료 중인 세션의 요청 제외   * 봇 테스터의 최초 연결 전환에서만 연결 콜백과 생존 검사 실행     * 종료 경합 중 연결 상태 복원 방지     * 생존 검사 작업에 취소 토큰을 미리 전달   * 연결 재요청, 중복 ACK 및 종료 경합 회귀 테스트 추가

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
