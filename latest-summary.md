<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `30e9ac6eca8c`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 183.687 µs | 217.900 µs | +0.28% | 243.300 µs | +2.23% | 1.517 ms |
| TX/RX Loss 10% | 1,000 × 5 | 6.049 ms | 32.402 ms | +0.19% | 69.261 ms | +4.36% | 226.244 ms |

Measured at (UTC): `2026-08-29T06:15:24.2181975+00:00`  
Commit log: * 1000클라이언트 폐루프 RTT 스트레스 벤치마크 추가   * 클라이언트별 요청 하나만 유지하는 closed-loop 부하 모델 구현   * 32바이트 echo payload 송수신 및 응답 내용 검증 추가   * 워밍업과 측정 구간을 분리하고 RTT/s 및 P50/P95/P99/P99.9 통계 수집   * 타임아웃, 잘못된 응답, 송신 실패 및 연결 해제 원인 집계   * 프로세스 CPU와 메모리 사용량 및 JSON 결과 출력 지원   * 재전송 간격과 최대 횟수를 세션별로 설정할 수 있도록 개선   * PacketWaiterRegistry가 신규 waiter를 동일 응답으로 완료하는 경쟁 조건 수정   * 서버의 최대 세션 수를 1100개로 확장   * 스트레스 벤치마크 실행 방법과 단위 테스트 추가   * CommonCode 메모리 풀 수정 커밋 반영

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
