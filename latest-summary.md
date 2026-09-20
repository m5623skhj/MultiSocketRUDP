<!-- RTT_BENCHMARK_SUMMARY_MARKER -->
## RTT Benchmark Result

Commit: `ed19d087967f`  
Server: `Release /O2`, IO worker: `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME`  
Server threads: `1`  
BotTester: `Release / Optimize=true`

| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loss 0% | 1,000 × 5 | 175.629 µs | 219.200 µs | +62.37% | 241.200 µs | +48.16% | 2.054 ms |
| TX/RX Loss 10% | 1,000 × 5 | 5.791 ms | 32.352 ms | -0.84% | 64.541 ms | +0.34% | 165.256 ms |

Measured at (UTC): `2026-09-20T09:12:55.446369+00:00`  
Commit log: * 공통 패킷 생성·CI 동기화 검사와 BotTester 구조체·컨테이너 지원 통합   * main 기준 rebase 및 패킷 생성기·문서 충돌 해소   * C++ 스키마 검증과 안전한 역직렬화를 유지하며 C# 중첩 스키마·송신 직렬화 추가   * 순서 기반 패킷 ID·기존 수동 핸들러 보존 및 생성 결과 검사 연결   * C++ 테스트 오브젝트·fixture 타입 충돌 해소와 양쪽 직렬화 회귀 검증 추가

Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.
