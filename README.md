# RTT benchmark history

The JSON file keeps the complete official history. Charts and the table render the latest 10 `main` measurements.

## Packet loss 0%

![RTT loss 0%](./rtt-loss-0.svg)

## BotTester TX/RX loss 10%

![RTT loss 10%](./rtt-loss-10.svg)

## Recent measurements

| Date (UTC) | Commit | Commit log | Loss 0% P95 | Loss 0% P99 | TX/RX loss 10% P95 | TX/RX loss 10% P99 |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| 2026-09-09 | `dd9dfaf` | * 실패한 테스트 수정 | 0.215 ms | 0.240 ms | 32.459 ms | 64.027 ms |
| 2026-09-08 | `546fc8d` | * 이전 세션의 재전송 작업이 재사용된 세션에 영향을 주는 경쟁 조건 수정   * generation 검사와 송신 작업 등록을 동일한 mutex로 보호   * 재전송 처리와 실패 정리가 완료될 때까지 세션 해제 지연   * 이전 generation 및 종료 중인 세션의 재전송 작업 거절   * 세션 재사용과 재전송 처리 중 해제 경쟁 회귀 테스트 추가   * 재전송 작업의 세션 수명 보호 문서 갱신 | 0.180 ms | 0.197 ms | 32.433 ms | 64.645 ms |
| 2026-09-08 | `cccba6b` | * 송신 준비가 끝나기 전에 세션의 암호화 자원을 해제 문제 수정 | 0.151 ms | 0.173 ms | 32.280 ms | 63.482 ms |
| 2026-09-08 | `6a8a448` | * 빌드 툴 버전 업 | 0.219 ms | 0.251 ms | 31.927 ms | 64.570 ms |
| 2026-08-29 | `30e9ac6` | * 1000클라이언트 폐루프 RTT 스트레스 벤치마크 추가   * 클라이언트별 요청 하나만 유지하는 closed-loop 부하 모델 구현   * 32바이트 echo payload 송수신 및 응답 내용 검증 추가   * 워밍업과 측정 구간을 분리하고 RTT/s 및 P50/P95/P99/P99.9 통계 수집   * 타임아웃, 잘못된 응답, 송신 실패 및 연결 해제 원인 집계   * 프로세스 CPU와 메모리 사용량 및 JSON 결과 출력 지원   * 재전송 간격과 최대 횟수를 세션별로 설정할 수 있도록 개선   * PacketWaiterRegistry가 신규 waiter를 동일 응답으로 완료하는 경쟁 조건 수정   * 서버의 최대 세션 수를 1100개로 확장   * 스트레스 벤치마크 실행 방법과 단위 테스트 추가   * CommonCode 메모리 풀 수정 커밋 반영 | 0.218 ms | 0.243 ms | 32.402 ms | 69.261 ms |
| 2026-08-26 | `611752b` | * 주석 최신화 * 클라이언트 수신 패킷 시퀀스 동기화 수정 | 0.217 ms | 0.238 ms | 32.342 ms | 66.364 ms |
| 2026-08-26 | `61833d0` | * 주석 없던 테스트들에 테스트 목적 주석 추가 | 0.225 ms | 0.259 ms | 32.228 ms | 64.554 ms |
| 2026-08-26 | `f72bf31` | * 인코딩 잘못된 주석 수정 | 0.216 ms | 0.242 ms | 32.405 ms | 64.146 ms |
| 2026-08-24 | `640064d` | * Schannel TLS 핸드셰이크 설정 및 실패 진단 개선 | 0.222 ms | 0.249 ms | 32.340 ms | 64.168 ms |
| 2026-08-09 | `cabf791` | * 재전송 Wake 이벤트 신호 실패 처리를 수정   * NULL 핸들을 성공으로 처리하던 예외 제거   * SetEvent 결과를 직접 반환해 오류 코드 보존 | 0.207 ms | 0.237 ms | 32.469 ms | 63.264 ms |

Last updated by `dd9dfaf3428e9d65703fb5beeb78b9763153db56` at 2026-09-09T03:59:06.6276371+00:00.
