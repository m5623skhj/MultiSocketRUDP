<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `d5186d0cd980`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 125.756 µs | 167.000 µs | +12.01% | 208.700 µs | +17.64% | 7.472 ms |
| TX/RX Loss 10% | 1,000 × 5 | 5.928 ms | 32.435 ms | +0.60% | 64.253 ms | -0.30% | 225.275 ms |

Measured at (UTC): `2026-09-10T15:51:13.951849+00:00`  
Commit log: * 동시 송신 시 시퀀스 발급 순서를 보장하고 패킷 헤더 구성 경로 분리   * 사용자 직렬화 이후 대기열 락 안에서 시퀀스 발급과 FIFO 등록 여부 결정   * 일반 패킷의 헤더 공간을 확보하고 packetId를 받는 송신 함수에서 헤더 완성   * 하트비트와 ACK 송신 경로를 분리하고 임시 시퀀스 및 덮어쓰기 제거   * 동시 송신 정체, 재진입, 하트비트, 본문 보존 및 ACK 회귀 테스트 추가

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
