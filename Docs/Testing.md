# 테스트 허브

> 검증하려는 범위에 맞는 테스트 문서와 실행 명령을 선택하는 진입점이다.

---

## 빠른 선택

| 확인하려는 범위 | 문서 | 대표 실행 대상 |
|---|---|---|
| C++ 코어 컴포넌트 | [유닛 테스트](Testing/UnitTests.md) | `CoreTest.exe` |
| BotTester C# 로직 | [유닛 테스트](Testing/UnitTests.md) | `dotnet test` |
| 실제 서버·클라이언트 연결 | [통합 테스트](Testing/IntegrationTests.md) | `IntegrationTest.exe` |
| C++/C# 패킷 호환성 | [통합 테스트](Testing/IntegrationTests.md) | C++ `PacketCryptoTest` + C# `ProtocolInteropTest` |
| PR에서 어떤 검사가 실행되는지 | [CI 가이드](Testing/CI.md) | `build-and-test` |
| PR·`main`의 RTT 성능 추세 | [CI 가이드 — RTT 성능 벤치마크](Testing/CI.md#rtt-성능-벤치마크) | `RTT Benchmark` |
| 구현 코드까지 한 번에 추적 | [상세 레퍼런스](Testing/TestingReference.md) | 전체 테스트 구조 |

---

## 최소 실행 순서

1. 변경한 컴포넌트의 유닛 테스트를 먼저 실행한다.
2. 네트워크, TLS, 세션 상태, 재전송, 패킷 포맷 변경이면 관련 통합 시나리오를 실행한다.
3. C++/C# 공통 포맷 변경이면 양쪽 protocol interop 테스트를 실행한다.
4. PR 전에는 [CI 가이드](Testing/CI.md)에서 경로별 실행 범위와 필수 체크를 확인한다.
5. 서버 I/O, 재전송, 타이밍 관련 변경이면 RTT 결과의 P95/P99 추세도 확인한다.

통합 테스트는 실제 스레드, UDP 소켓, TLS, timeout을 사용한다. CI 부하에 따른 flakiness를 피하려면 실패 시 전체 suite를 반복하기 전에 단일 `--gtest_filter`로 재현한다.

---

## 완료 기준

- 변경된 계약을 직접 검증하는 테스트가 있다.
- 실패 시나리오와 정상 시나리오를 구분해 확인했다.
- 테스트가 남긴 프로세스, 인증서, 포트 점유가 다음 테스트에 영향을 주지 않는다.
- 프로토콜 변경은 C++과 C# 결과가 같은 vector를 기준으로 일치한다.
- PR 필수 체크 `build-and-test`가 성공한다.

---

## 관련 문서

- [개발·테스트 보조 스크립트](Tools/DevelopmentScripts.md)
- [문제 해결](Troubleshooting.md)
- [문서 읽기 가이드](ReadingGuide.md)
