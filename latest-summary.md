<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `7287ccd1f6cf`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 176.860 µs | 231.300 µs | +10.78% | 275.000 µs | +20.19% | 8.543 ms |
| TX/RX Loss 10% | 1,000 × 5 | 6.043 ms | 32.907 ms | -1.47% | 64.873 ms | -1.17% | 297.318 ms |

Measured at (UTC): `2026-09-13T15:30:58.4797465+00:00`  
Commit log: * 멀티스레드 안정성 검증을 위한 야간·수동 CI 추가   * 동시 송수신·연결 해제·세션 풀 회수 및 재사용 통합 테스트 추가   * 케이스별 독립 프로세스 반복 실행과 최초 실패 로그·재현 명령 보존   * 실행 시간 제한 및 timeout 발생 시 자식 프로세스 종료 처리   * 실행기 실패 판정 검증과 안정성 검사 가이드 추가

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
