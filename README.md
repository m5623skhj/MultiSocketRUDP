# RTT benchmark history

rtt-history.json keeps the complete official Ping/Pong history. Its charts and table render the latest 10 `main` measurements.

## Packet loss 0%

![RTT loss 0%](./rtt-loss-0.svg)

## BotTester TX/RX loss 10%

![RTT loss 10%](./rtt-loss-10.svg)

## Sequential channel RTT and response rate

Standalone v3 compares the latest 10 compatible same-commit pairs: the same 9-byte Echo, one outstanding request, and no fixed send interval. An unreliable request timeout counts as missing and permits the next request; a reliable timeout fails the measurement. Earlier versions are not joined to this trend.

Mixed v1 keeps its separate 10:1 load with a 10 ms delay per batch. Missing responses are excluded from RTT; read latency together with response rate. Channel history retains the latest 150 scenario results.

![Reliable vs unreliable, sequential Echo](./channel-comparison.svg)

![Mixed-load unreliable RTT and response rate](./channel-mixed.svg)

[Channel summary](./channel-summary.md) · [Channel history](./channel-history.json)

## Recent measurements

| Date (UTC) | Commit | Commit log | Loss 0% P95 | Loss 0% P99 | TX/RX loss 10% P95 | TX/RX loss 10% P99 |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| 2026-11-16 | `8b65e4b` | * 서버와 클라이언트 TLS 변경 이력 통합 | 0.137 ms | 0.156 ms | 32.461 ms | 65.435 ms |
| 2026-11-15 | `3b21e76` | * 통합 클라이언트의 Windows 소켓 헤더 포함 순서 수정 | 0.213 ms | 0.242 ms | 32.449 ms | 64.507 ms |
| 2026-11-15 | `ac94f2c` | * 원격 라이브러리 변경과 진단 로그 개선 통합 | 0.236 ms | 0.308 ms | 32.494 ms | 64.719 ms |
| 2026-11-11 | `69aa11b` | * README의 비신뢰성 RTT 테스트 그래프 단위 수정 | 0.220 ms | 0.254 ms | 32.445 ms | 64.516 ms |
| 2026-11-11 | `b5fed3c` | * RTT 벤치마크 테스트를 일관성 있게 그래프가 보이도록 수정   * 비신뢰성 송신 테스트를 10회 기준에서 1회 기준으로 수정   * 비신뢰성 송신 테스트를 10ms 대기에서 수신 즉시 재송신으로 수정 | 0.212 ms | 0.273 ms | 32.665 ms | 64.601 ms |
| 2026-11-11 | `6a05d49` | * CI 테스트 인증서 등록 대기 문제 수정 | 0.140 ms | 0.160 ms | 32.160 ms | 66.918 ms |
| 2026-11-06 | `99db792` | * 던전 비신뢰 패킷의 역순 수신과 재생 방지 지원 | 0.242 ms | 0.271 ms | 32.271 ms | 63.531 ms |
| 2026-11-06 | `4f0dfe9` | * 통합 테스트 CI의 TLS 인증서 신뢰 설정 수정 | 0.229 ms | 0.261 ms | 32.307 ms | 63.024 ms |
| 2026-10-28 | `f3da8b1` | * 문서 전면 최신화 및 코드 대조 오류 정정 | 0.157 ms | 0.180 ms | 32.290 ms | 64.549 ms |
| 2026-10-27 | `0cfee60` | * 원격 라이브러리 수정과 외부 접속·송신 큐 계측 변경 통합 | 0.228 ms | 0.253 ms | 32.147 ms | 64.236 ms |

Last updated by `8b65e4b98e780b66d2427f6c302789e08626cf29` at 2026-11-16T15:27:07.4254879+00:00.
