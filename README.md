# RTT benchmark history

The JSON file keeps the complete official history. Charts and the table render the latest 10 `main` measurements.

## Packet loss 0%

![RTT loss 0%](./rtt-loss-0.svg)

## BotTester TX/RX loss 10%

![RTT loss 10%](./rtt-loss-10.svg)

## Recent measurements

| Date (UTC) | Commit | Commit log | Loss 0% P95 | Loss 0% P99 | TX/RX loss 10% P95 | TX/RX loss 10% P99 |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| 2026-08-26 | `611752b` | * 주석 최신화 * 클라이언트 수신 패킷 시퀀스 동기화 수정 | 0.217 ms | 0.238 ms | 32.342 ms | 66.364 ms |
| 2026-08-26 | `61833d0` | * 주석 없던 테스트들에 테스트 목적 주석 추가 | 0.225 ms | 0.259 ms | 32.228 ms | 64.554 ms |
| 2026-08-26 | `f72bf31` | * 인코딩 잘못된 주석 수정 | 0.216 ms | 0.242 ms | 32.405 ms | 64.146 ms |
| 2026-08-24 | `640064d` | * Schannel TLS 핸드셰이크 설정 및 실패 진단 개선 | 0.222 ms | 0.249 ms | 32.340 ms | 64.168 ms |
| 2026-08-09 | `cabf791` | * 재전송 Wake 이벤트 신호 실패 처리를 수정   * NULL 핸들을 성공으로 처리하던 예외 제거   * SetEvent 결과를 직접 반환해 오류 코드 보존 | 0.207 ms | 0.237 ms | 32.469 ms | 63.264 ms |
| 2026-08-07 | `cb4ed7d` | * 주석 수정 | 0.221 ms | 0.252 ms | 32.396 ms | 63.201 ms |
| 2026-08-07 | `536de21` | * 코드 정리 * 클라이언트 TLS 보안 강화 | 0.180 ms | 0.205 ms | 32.289 ms | 64.362 ms |
| 2026-08-04 | `34b8102` | * README RTT 표에서 현재 RTT를 알 수 있도록 추가 | 0.162 ms | 0.192 ms | 32.416 ms | 62.135 ms |
| 2026-08-04 | `f75b724` | * TLS 파일 분리 | 0.183 ms | 0.200 ms | 32.400 ms | 63.983 ms |
| 2026-08-03 | `03293c8` | * 주석 수정 및 추가 | 0.205 ms | 0.248 ms | 32.606 ms | 64.302 ms |

Last updated by `611752b722902f89706c7e1232d17b7140588f17` at 2026-08-26T11:59:45.8524075+00:00.
