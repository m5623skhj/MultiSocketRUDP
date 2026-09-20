<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `48b1829de289`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 114.791 µs | 141.000 µs | -35.68% | 193.600 µs | -19.73% | 1.734 ms |
| TX/RX Loss 10% | 1,000 × 5 | 6.246 ms | 32.441 ms | +0.27% | 67.611 ms | +4.76% | 401.216 ms |

Measured at (UTC): `2026-09-20T09:21:20.1284913+00:00`  
Commit log: * 클라이언트 reliable 송신 대기 큐의 ACK 경합으로 인한 정체 수정   * 콘텐츠 송신을 pending 큐로 통합하고 삽입 후 flush 수행   * 윈도우 확인과 콘텐츠 등록을 직렬화해 중복 슬롯 사용 방지   * outstanding 개수의 BYTE 변환 제거 * TLS 평문 누적 오류 수정

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
