# RTT benchmark history

The JSON file keeps the complete official history. Charts and the table render the latest 10 `main` measurements.

## Packet loss 0%

![RTT loss 0%](./rtt-loss-0.svg)

## BotTester TX/RX loss 10%

![RTT loss 10%](./rtt-loss-10.svg)

## Recent measurements

| Date (UTC) | Commit | Commit log | Loss 0% P95 | Loss 0% P99 | TX/RX loss 10% P95 | TX/RX loss 10% P99 |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| 2026-08-09 | `cabf791` | * 재전송 Wake 이벤트 신호 실패 처리를 수정   * NULL 핸들을 성공으로 처리하던 예외 제거   * SetEvent 결과를 직접 반환해 오류 코드 보존 | 0.207 ms | 0.237 ms | 32.469 ms | 63.264 ms |
| 2026-08-07 | `cb4ed7d` | * 주석 수정 | 0.221 ms | 0.252 ms | 32.396 ms | 63.201 ms |
| 2026-08-07 | `536de21` | * 코드 정리 * 클라이언트 TLS 보안 강화 | 0.180 ms | 0.205 ms | 32.289 ms | 64.362 ms |
| 2026-08-04 | `34b8102` | * README RTT 표에서 현재 RTT를 알 수 있도록 추가 | 0.162 ms | 0.192 ms | 32.416 ms | 62.135 ms |
| 2026-08-04 | `f75b724` | * TLS 파일 분리 | 0.183 ms | 0.200 ms | 32.400 ms | 63.983 ms |
| 2026-08-03 | `03293c8` | * 주석 수정 및 추가 | 0.205 ms | 0.248 ms | 32.606 ms | 64.302 ms |
| 2026-08-03 | `cb43718` | * Receive context 큐 종료 행잉 수정   * 재사용 노드의 stale link로 소멸자가 무한 순회하던 CListBaseQueue 제거   * worker별 receive context 큐의 producer와 consumer 접근 동기화   * 큐 소멸 순환과 남은 개수 확인 경쟁 조건 제거   * IntegrationTest 종료 단계의 간헐적 타임아웃 방지 | 62.839 ms | 63.596 ms | 102.392 ms | 153.679 ms |

Last updated by `cabf79193e81f976e9e7459fd57b248c873050a7` at 2026-08-09T07:55:10.1873707+00:00.
