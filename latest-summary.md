<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `26e1e6c2d27a`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 191.289 µs | 224.200 µs | +65.58% | 252.000 µs | +61.64% | 1.816 ms |
| TX/RX Loss 10% | 1,000 × 5 | 6.445 ms | 32.833 ms | +1.40% | 67.290 ms | +4.16% | 401.706 ms |

Measured at (UTC): `2026-10-02T09:41:39.0626366+00:00`  
Commit log: * 수신 패킷 복호화 후 인증 태그가 읽기 범위에 남는 버그 수정   * 인증 성공 시 버퍼 끝 위치를 인증 태그 앞으로 조정   * 콘텐츠 소비 후 잔여 크기 및 범위 초과 읽기 회귀 검증   * 신뢰성·비신뢰성 수신 API 검사와 관련 문서 보완

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
