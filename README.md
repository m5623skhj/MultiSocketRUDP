# RTT benchmark history

The JSON file keeps the complete official history. Charts and the table render the latest 10 `main` measurements.

## Packet loss 0%

![RTT loss 0%](./rtt-loss-0.svg)

## BotTester TX/RX loss 10%

![RTT loss 10%](./rtt-loss-10.svg)

## Unreliable channel RTT and response rate

Latest 10 measurements compatible with the latest result in each scenario. Missing responses are excluded from RTT; read latency together with response rate.

![Unreliable-only RTT and response rate](./channel-unreliable-only.svg)

![Mixed-load unreliable RTT and response rate](./channel-mixed.svg)

[Channel summary](./channel-summary.md) · [Channel history](./channel-history.json)

## Recent measurements

| Date (UTC) | Commit | Commit log | Loss 0% P95 | Loss 0% P99 | TX/RX loss 10% P95 | TX/RX loss 10% P99 |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| 2026-10-27 | `ce5083e` | * 호스트 로컬 RUDP 세션 접속 옵션 지원 | 0.217 ms | 0.260 ms | 32.431 ms | 65.886 ms |
| 2026-10-24 | `bb59568` | * 원격 RUDP 버퍼 및 ACK 종료 경합 수정 통합 | 0.141 ms | 0.158 ms | 32.496 ms | 64.258 ms |
| 2026-10-22 | `fa51ffb` | * NetBuffer 지연 초기화 연동과 ASan 회귀 검증 보완 | 0.166 ms | 0.188 ms | 32.619 ms | 64.520 ms |
| 2026-10-21 | `0b8ab0d` | * RUDP 진단 로그 조건문 범위와 소스 인코딩 수정 | 0.171 ms | 0.202 ms | 32.627 ms | 64.184 ms |
| 2026-10-20 | `caf723f` | * 클라이언트 응답 훅 경고 및 공통 코드 참조 정리 | 0.234 ms | 0.252 ms | 32.610 ms | 64.612 ms |
| 2026-10-09 | `622f8a0` | * 코드 정리 | 0.226 ms | 0.245 ms | 31.936 ms | 63.801 ms |
| 2026-10-04 | `37cc2df` | * 클라이언트 코어 종료 시 연결·송수신 대기와 로그 스레드 정리 개선 | 0.226 ms | 0.246 ms | 32.411 ms | 64.753 ms |
| 2026-10-02 | `26e1e6c` | * 수신 패킷 복호화 후 인증 태그가 읽기 범위에 남는 버그 수정   * 인증 성공 시 버퍼 끝 위치를 인증 태그 앞으로 조정   * 콘텐츠 소비 후 잔여 크기 및 범위 초과 읽기 회귀 검증   * 신뢰성·비신뢰성 수신 API 검사와 관련 문서 보완 | 0.224 ms | 0.252 ms | 32.833 ms | 67.290 ms |
| 2026-10-01 | `fc496f9` | * 서버 종료 시 대기 중인 브로커 소켓 정리 지연 개선   * 전체 워커 중지 요청 후 큐 소켓을 먼저 닫고 워커 종료 대기   * 종료 테스트 실패 메시지에 소켓 인덱스와 실제 대기 시간 추가   * Debug 빌드 및 종료 테스트 5회·관련 TLS 테스트 2개 통과 | 0.135 ms | 0.156 ms | 32.380 ms | 64.606 ms |
| 2026-09-21 | `4fbb027` | * 클라이언트 종료·재시작 동시성 및 설정 검증 문제 수정   * reliable 송신의 연결 세대 검증과 버퍼 수명 보호 적용   * 종료 요청과 정리를 분리하고 Stop의 완료 대기 보장   * 재전송 한도 초과를 공통 종료 경로로 연결   * 재시작 시 송신 상태 초기화 및 이전 TLS 자원 해제   * 설정 파일 읽기 실패와 숫자 범위·오버플로 검증   * 재시작·동시 종료·CONNECT ACK 유실 회귀 테스트 추가   * Debug·Release 빌드 및 관련 테스트 통과 | 0.162 ms | 0.201 ms | 32.056 ms | 63.746 ms |

Last updated by `ce5083e1732e77818deb2ea733390ecf3b125020` at 2026-10-27T18:47:00.6438618+00:00.
