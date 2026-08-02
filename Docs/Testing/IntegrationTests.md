# 통합·프로토콜 테스트

> 실제 서버와 별도 클라이언트 프로세스, TLS, UDP, timeout을 포함하는 검증 절차를 정리한다.

---

## 통합 테스트 실행

`IntegrationTest.exe`가 테스트 서버를 시작하고 `IntegrationClientHarness.exe`를 별도 프로세스로 실행한다.

```powershell
msbuild .\MultiSocketRUDP\MultiSocketRUDP.sln /t:IntegrationTest /p:Configuration=Debug /p:Platform=x64
.\MultiSocketRUDP\x64\Debug\IntegrationTest.exe
```

인증서 생성과 빌드를 함께 수행하려면 다음 스크립트를 사용한다.

```powershell
.\MultiSocketRUDP\Tool\BuildIntegrationTestAndRun.bat
```

통합 테스트용 `MultiSocketRUDP/IntegrationTest/TestCert.pfx`는 커밋하지 않는다. 필요하면 `MultiSocketRUDP/Tool/ForTLS/CreateDevTLSPfx.bat`로 생성한다.

---

## 시나리오 선택

| 시나리오 | 핵심 계약 |
|---|---|
| `connect` | TLS 세션 발급 후 UDP CONNECT와 연결 통계 |
| `reserve-timeout` | CONNECT가 없는 예약 세션의 pool 반환 |
| `echo` | 요청·응답 payload 왕복 |
| `ping` | 등록된 애플리케이션 handler를 통한 Ping/Pong 왕복 |
| `drop-ack` | ACK 누락 후 재전송 한계 disconnect |
| `disconnect` | 명시적 disconnect와 release 통계 |
| `stop` | 클라이언트 stop의 정상 종료 |
| `multi-echo` | 복수 클라이언트 동시 왕복 |
| `ordered-burst` | 연속 요청의 순서 보장 |

실패 재현에는 단일 filter를 우선 사용한다.

```powershell
.\MultiSocketRUDP\x64\Debug\IntegrationTest.exe --gtest_filter=IntegrationFixture.ConnectHandshakeCompletesAndSessionCountsUpdate
```

### 타이밍과 thread-safety 주의

- timeout을 너무 짧게 잡으면 CI runner 부하에 따라 flaky해질 수 있다.
- reserved timeout, retransmission timeout, alive check 주기는 함께 검토한다.
- 테스트 종료 전에 background thread와 child process가 모두 정리됐는지 확인한다.
- 공유 통계는 최종 값만 기다리지 말고 어떤 이벤트 이후 값이 유효해지는지 명시한다.

---

## C++/C# 프로토콜 상호운용

`MultiSocketRUDPBotTester/ProtocolInteropTest/ProtocolInteropVector.json`을 C++ `PacketCryptoTest`와 C# `ProtocolInteropTest`가 함께 사용한다. 키, salt, sequence, 방향, core/full 구분, packet type, packet ID, 평문과 예상 패킷이 양쪽에서 일치해야 한다.

```powershell
msbuild .\MultiSocketRUDP\MultiSocketRUDP.sln /t:CoreTest /p:Configuration=Debug /p:Platform=x64
.\MultiSocketRUDP\x64\Debug\CoreTest.exe --gtest_filter=PacketCryptoTest.AesGcmMatchesCppCSharpGoldenVectors

dotnet build .\MultiSocketRUDPBotTester\MultiSocketRUDPBotTester.sln --configuration Debug
dotnet run --project .\MultiSocketRUDPBotTester\ProtocolInteropTest\ProtocolInteropTest.csproj --configuration Debug
```

앞의 첫 두 명령은 C++ vector consumer를, 뒤의 두 명령은 C# vector consumer를 검증한다. `BotTester.yml`은 C# 쪽만 실행하며, 공용 vector가 변경되면 `CI.yml`의 경로 분류가 Native GTest와 BotTester workflow를 모두 호출한다.

패킷 오프셋, nonce, AAD, AuthTag, byte order를 변경하면 일반 통합 테스트 성공만으로는 충분하지 않다. 공용 vector와 양쪽 구현을 함께 갱신하고 C++·C# 테스트를 모두 실행한다.

---

## 더 자세히 보기

- [테스트 상세 레퍼런스 — 통합 테스트](TestingReference.md#통합-테스트)
- [테스트 상세 레퍼런스 — 프로토콜 상호운용](TestingReference.md#cc-프로토콜-상호운용-테스트)
- [CryptoSystem](../Common/CryptoSystem.md)
- [SessionLifecycle](../Server/SessionLifecycle.md)
