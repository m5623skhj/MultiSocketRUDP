# Testing

> MultiSocketRUDP의 유닛 테스트와 통합 테스트 구성, 실행 방법, CI 주의점을 정리한다.

---

## 테스트 구성

현재 테스트는 세 범위로 구성한다.

- `CoreTest`
  - 서버 코어 내부 컴포넌트를 대상으로 하는 GoogleTest 기반 유닛 테스트
  - 패킷 매니저, 흐름 제어, 수신 윈도우, IO 핸들러, 패킷 프로세서, 세션 상태 머신 등을 검증
- `IntegrationTest`
  - 실제 서버 코어와 클라이언트 하네스를 함께 실행하는 통합 테스트
  - TLS 세션 브로커, UDP CONNECT, 요청/응답, 클라이언트 disconnect/stop, 재전송 실패 disconnect, 다중 클라이언트, 순서 보장 흐름을 검증
- `ProtocolInteropTest`
  - BotTester의 C# 패킷 암호화 구현을 검증하는 실행형 테스트
  - C++ CoreTest와 공용 protocol vector를 사용해 AES-GCM 결과 호환성을 검증

`IntegrationClientHarness`는 `IntegrationTest`가 별도 프로세스로 실행하는 테스트용 클라이언트 실행 파일이다.

---

## 유닛 테스트

프로젝트:

```text
MultiSocketRUDP/CoreTest/CoreTest.vcxproj
```

주요 테스트 범위:

- `PacketManagerTest`
- `PacketCryptoTest`
- `CryptoHelperTest`
- `RUDPFlowControllerTest`
- `RUDPFlowManagerTest`
- `RUDPIOHandlerTest`
- `RUDPPacketProcessorTest`
- `RUDPReceiveWindowTest`
- `RUDPThreadManagerTest`
- `RetransmissionTimeoutEstimatorTest`
- `RingBufferTest`
- `SendPacketInfoTest`
- `RUDPSessionTest`
- `RUDPSessionManagerTest`
- `SessionCryptoContextTest`
- `SessionPacketOrdererTest`
- `SessionRIOContextTest`
- `SessionSendContextTest`
- `SessionStateMachineTest`
- `TickerTimerEventTest`

최근 보강된 `CoreTest` 검증 포인트:

- `RUDPPacketProcessorTest`
  - `PacketCryptoHelper`의 실제 AES-GCM encode/decode 경로를 사용해 `SEND_TYPE` 수신 성공 시 `OnRecvPacket` 호출과 TPS 증가를 검증한다.
  - `SEND_TYPE` 복호화가 성공했더라도 세션의 `OnRecvPacket` 처리 결과가 실패이면 TPS가 증가하지 않는지 검증한다.
  - `SEND_REPLY_TYPE` 복호화 성공 시 `OnSendReply`로 전달되는지 검증한다.
  - UDP 주소 버퍼가 `sockaddr_in`보다 짧은 경우, 패킷 본문 처리 전에 조기 반환되는지 검증한다.
- `RUDPSessionManagerTest`
  - 유효하지 않은 `sessionId` 반환 요청을 거부한다.
  - `RELEASING` 상태가 아닌 세션 반환 요청을 거부한다.
  - `BY_RETRANSMISSION` disconnect가 전체 disconnect 및 retransmission disconnect 통계를 함께 증가시키는지 검증한다.
  - `BY_ABORT_RESERVED`는 연결된 세션의 disconnect 통계로 계산하지 않는지 검증한다.

빌드:

```powershell
msbuild .\MultiSocketRUDP.sln /t:CoreTest /p:Configuration=Debug /p:Platform=x64
```

실행:

```powershell
.\x64\Debug\CoreTest.exe
```

---

## 통합 테스트

프로젝트:

```text
MultiSocketRUDP/IntegrationTest/IntegrationTest.vcxproj
MultiSocketRUDP/IntegrationClientHarness/IntegrationClientHarness.vcxproj
```

통합 테스트는 `IntegrationTest.exe`가 테스트 서버를 시작하고, `IntegrationClientHarness.exe`를 별도 프로세스로 실행해 클라이언트 시나리오를 검증한다.

현재 시나리오:

- `connect`
  - SessionBroker에서 세션 정보를 받은 뒤 CONNECT 핸드셰이크가 완료되는지 검증
  - 서버 connected session count와 `OnConnected` 통계 증가를 확인
- `reserve-timeout`
  - 세션을 예약만 하고 CONNECT를 보내지 않는 클라이언트 흐름 검증
  - Debug 빌드에서 reserved session timeout을 낮춰 unused pool로 반환되는지 확인
- `echo`
  - 클라이언트가 문자열 요청 패킷을 보내고 서버 응답을 받는 왕복 흐름 검증
  - 서버가 마지막 echo payload를 정확히 기록했는지 확인
- `drop-ack`
  - 클라이언트가 데이터 패킷 reply 자동 응답을 끈 뒤 서버 재전송 실패 disconnect를 검증
- `disconnect`
  - 클라이언트가 `DisconnectClient()`를 호출한 뒤 서버 disconnect/release 통계와 connected session count 정리를 검증
- `stop`
  - 클라이언트가 연결 후 `StopClient()`로 종료될 때 harness가 강제 종료 없이 정상 완료되는지 검증
- `multi-echo`
  - 여러 클라이언트가 동시에 연결하고 echo 요청/응답을 수행하는 흐름을 검증
- `ordered-burst`
  - 클라이언트가 순서가 있는 요청 5개를 연속 전송하고 응답 순서가 유지되는지 검증

빌드:

```powershell
msbuild .\MultiSocketRUDP.sln /t:IntegrationTest /p:Configuration=Debug /p:Platform=x64
```

실행:

```powershell
.\x64\Debug\IntegrationTest.exe
```

또는 빌드와 인증서 생성을 포함한 스크립트를 사용한다.

```powershell
.\Tool\BuildIntegrationTestAndRun.bat
```

---

## C++/C# 프로토콜 상호운용 테스트

공용 vector:

```text
MultiSocketRUDPBotTester/ProtocolInteropTest/ProtocolInteropVector.json
```

C++ `PacketCryptoTest`와 C# `ProtocolInteropTest`가 같은 키, salt, sequence, 평문 및 예상 패킷을 읽는다.

BotTester 전체 빌드와 protocol test 실행:

```powershell
dotnet build .\MultiSocketRUDPBotTester\MultiSocketRUDPBotTester.sln --configuration Debug
dotnet run --project .\MultiSocketRUDPBotTester\ProtocolInteropTest\ProtocolInteropTest.csproj --configuration Debug
```

---

## TLS 테스트 인증서

통합 테스트는 PFX 인증서 파일을 사용한다.

```text
MultiSocketRUDP/IntegrationTest/TestCert.pfx
```

이 파일은 저장소에 커밋하지 않는다. 필요할 때 아래 스크립트로 생성한다.

```powershell
.\Tool\ForTLS\CreateDevTLSPfx.bat
```

GitHub Actions에서는 `GoogleTest.yml`에서 테스트 전에 이 스크립트를 실행한다.

```yaml
- name: Generate IntegrationTest certificate
  shell: cmd
  run: MultiSocketRUDP\Tool\ForTLS\CreateDevTLSPfx.bat
```

---

## CI 흐름

PR CI는 dispatcher와 두 개의 재사용 workflow로 구성한다.

```text
.github/workflows/CI.yml          # 변경 경로 분류 및 최종 상태 집계
.github/workflows/GoogleTest.yml  # C++ Native 테스트
.github/workflows/BotTester.yml   # C# BotTester 빌드 및 프로토콜 테스트
```

`CI.yml`은 모든 PR에서 실행되고 변경 파일을 기준으로 필요한 workflow만 호출한다.

| 변경 경로 | Native GTest | BotTester |
|---|---:|---:|
| `MultiSocketRUDP/**`, C++ 테스트 및 submodule | 실행 | 미실행 |
| `MultiSocketRUDPBotTester/**` | 미실행 | 실행 |
| 공용 `ProtocolInteropVector.json` | 실행 | 실행 |
| `CI.yml` | 실행 | 실행 |
| 관련 없는 문서만 변경 | 미실행 | 미실행 |

### Native GTest

1. submodule 포함 checkout
2. NuGet restore
3. IntegrationTest 인증서 생성
4. GoogleTest 및 solution Debug x64 빌드
5. `CoreTest.exe`, `IntegrationTest.exe`만 실행
6. 실행 파일별 XML과 exit code 검증
7. 실패 테스트만 retry
8. PR comment와 OpenCppCoverage 결과 갱신

### BotTester

1. .NET 9 설정
2. `MultiSocketRUDPBotTester.sln` 전체 빌드
3. `ProtocolInteropTest.csproj` 빌드 및 실행
4. C++과 C#이 공용 `ProtocolInteropVector.json`을 기준으로 동일한 암호화 결과를 내는지 검증

### 필수 체크

Branch protection의 필수 체크는 최종 집계 job인 `build-and-test` 하나로 설정한다. 이 이름은 기존 branch protection과의 호환성을 위해 고정한다.

`build-and-test`는 모든 PR에서 항상 생성되며 다음 규칙으로 결과를 판정한다.

- 변경 경로상 필요한 테스트가 모두 성공하면 성공
- 관련 없는 테스트가 skip되면 성공
- 필요한 테스트가 실패, 취소 또는 비정상 skip되면 실패
- 변경 경로 분류가 실패하면 실패

`Expected — Waiting for status to be reported`가 계속 표시되면 branch protection이 더 이상 생성되지 않는 과거 check 이름을 요구하는지 확인한다. Repository settings의 required checks에는 `build-and-test`만 남기고 `CI Gate`, `Native GTest`, `Native GTest / build-and-test`, `BotTester Protocol Interop` 같은 이전 항목은 제거한다.

AI 리뷰 status는 필수 체크로 사용하지 않는다. 현재 `GeminiPRCommoentBot.yml`은 status context `ai-review-check`를 보고하지만, Gemini API의 일시적 503, rate limit, 응답 형식 실패 같은 외부 서비스 상태에 영향을 받을 수 있으므로 PR merge gate가 아니라 보조 리뷰 신호로만 사용한다.

서브모듈 checkout은 full fetch를 사용한다.

```yaml
submodules: recursive
fetch-depth: 0
```

---

## 파일 추가 주의

현재 `.gitignore`에는 광범위한 `*.cpp` ignore 규칙이 있다. 새 테스트 `.cpp` 파일을 추가하면 Git에 자동으로 잡히지 않을 수 있다.

프로젝트 파일에서 참조하는 새 `.cpp` 파일은 반드시 추적 상태를 확인한다.

```powershell
git status --short --ignored -- <path>
git ls-files -- <path>
```

ignored 상태인 테스트 소스는 필요한 파일만 강제로 추가한다.

```powershell
git add -f <path>
```

빌드 로그에서 아래 오류가 나오면 프로젝트 파일이 참조하는 소스가 원격 커밋에 포함되지 않은 상태일 가능성이 높다.

```text
error C1083: Cannot open source file
```

---

## 스레드와 타이밍 주의

통합 테스트는 실제 서버/클라이언트 스레드, UDP 소켓, TLS 핸드셰이크, 재전송 타이머를 사용한다.

- timeout 값을 너무 짧게 잡으면 CI runner 부하에 따라 flaky해질 수 있다.
- reserved session timeout, retransmission timeout, alive check 주기를 함께 고려해야 한다.
- 실패 재현 시에는 단일 테스트 필터로 먼저 확인한다.

```powershell
.\x64\Debug\IntegrationTest.exe --gtest_filter=IntegrationFixture.RequestResponseRoundTripReturnsExpectedPayload
```

---

## 관련 파일

- `MultiSocketRUDP/CoreTest`
- `MultiSocketRUDP/IntegrationTest`
- `MultiSocketRUDP/IntegrationClientHarness`
- `MultiSocketRUDP/Tool/ForTLS/CreateDevTLSPfx.bat`
- `.github/workflows/CI.yml`
- `.github/workflows/GoogleTest.yml`
- `.github/workflows/BotTester.yml`
- `MultiSocketRUDPBotTester/ProtocolInteropTest`

---

## 통합 테스트 상세

`IntegrationTest.exe`는 테스트 서버를 직접 시작하고, `IntegrationClientHarness.exe`를 별도 프로세스로 실행해 실제 클라이언트 시나리오를 검증한다.

```text
IntegrationTest
  - 테스트 서버 시작
  - 임시 옵션 파일 생성
  - IntegrationClientHarness 프로세스 실행
  - 서버 통계와 클라이언트 종료 코드를 검증

IntegrationClientHarness
  - TestableRUDPClient 래퍼 사용
  - connect, reserve-timeout, echo, drop-ack, disconnect, stop, multi-echo, ordered-burst 시나리오 실행
```

### IntegrationClientHarness

`MultiSocketRUDP/IntegrationClientHarness/main.cpp`의 테스트 실행 진입점이다.

| 함수 | 역할 |
|------|------|
| `GetOptionPath` | 실행 파일 위치 기준으로 기본 옵션 파일 경로를 만든다. |
| `GetArgumentValue` | `--client-core-option`, `--client-session-getter-option`, `--scenario` 같은 CLI 옵션 값을 읽는다. |
| `RunConnectScenario` | 클라이언트를 자동 CONNECT 모드로 시작하고 연결 완료를 기다린다. |
| `RunReserveOnlyScenario` | CONNECT를 보내지 않는 클라이언트를 시작해 서버의 reserved session timeout 흐름을 검증한다. |
| `RunEchoScenario` | 문자열 요청 패킷을 보내고 동일 payload 응답을 기다린다. |
| `RunDropAckScenario` | 데이터 패킷 reply를 비활성화해 서버 retransmission disconnect 흐름을 유도한다. |
| `RunDisconnectScenario` | 연결 후 `DisconnectClient()`를 호출해 서버 release 흐름을 유도한다. |
| `RunStopScenario` | 연결 후 `StopClient()`만 호출해 클라이언트 종료 정리를 검증한다. |
| `RunMultiEchoScenario` | 지정한 수의 클라이언트를 동시에 시작하고 각 클라이언트의 echo 왕복을 검증한다. |
| `RunOrderedBurstScenario` | 순서가 있는 요청 5개를 연속 전송하고 동일 순서의 응답을 기다린다. |
| `wmain` | wide-char CLI 인자를 파싱하고 시나리오별 실행 함수를 호출한다. |
| `main` | narrow-char 진입점이 필요한 빌드 환경에서 `wmain` 흐름으로 연결한다. |

`RunReserveOnlyScenario`와 `RunDropAckScenario`는 정상 사용 흐름이 아니라 서버의 timeout/retransmission 처리를 검증하기 위한 부정 시나리오다. `RunDisconnectScenario`와 `RunStopScenario`는 종료 경로 안정성을 검증하기 위한 별도 시나리오다.

### IntegrationTest 헬퍼

`MultiSocketRUDP/IntegrationTest/IntegrationTest.cpp`의 테스트 fixture와 헬퍼다.

| 함수 | 역할 |
|------|------|
| `TryInitializeClientTls` | 테스트 전에 TLS client credential 초기화가 가능한지 확인한다. |
| `GetRootRelativePath` | 테스트 실행 파일 기준으로 저장소 상대 경로를 만든다. |
| `GetTestOptionPath` | `IntegrationTest` 하위 테스트 옵션 파일 경로를 만든다. |
| `GetTestCertificatePath` | `TestCert.pfx` 경로를 반환한다. |
| `AcquireAvailableTcpPort` | 임시 SessionBroker용 TCP 포트를 확보한다. |
| `HasTestCertificateFile` | 통합 테스트용 PFX 인증서 존재 여부를 확인한다. |
| `CreateTestOptionFiles` | 테스트별 임시 SessionBroker/Client 옵션 파일을 생성한다. |
| `SetUpTestSuite` | Winsock을 초기화한다. |
| `TearDownTestSuite` | Winsock을 정리한다. |
| `SetUp` | 인증서, TLS 초기화, 임시 옵션 파일, 테스트 서버를 준비한다. |
| `TearDown` | 테스트 서버와 임시 상태를 정리한다. |
| `RunClientScenario` | harness 프로세스를 실행하고 timeout까지 완료를 기다린다. |
| `BuildClientArgs` | harness 실행 인자에 테스트 옵션 파일 경로를 추가한다. |
| `WaitUntil` | polling 기반 조건 대기 헬퍼다. |

`CreateTestOptionFiles`는 테스트 간 포트 충돌을 줄이기 위해 실행 시점에 가용 TCP 포트를 할당받고, 파일명에도 증가 ID를 붙인다.

### TestableRUDPClient

`MultiSocketRUDP/IntegrationTest/TestableRUDPClient.*`는 테스트에서만 사용하는 `RUDPClientCore` 래퍼다.

| 함수 | 역할 |
|------|------|
| `StartClient` | 옵션 파일 경로와 CONNECT 자동 전송 여부를 받아 클라이언트를 시작한다. |
| `StopClient` | 내부 `RUDPClientCore::Stop()`을 호출한다. |
| `SetAutoReplyDataPackets` | 데이터 패킷 reply 자동 전송 여부를 바꾼다. |
| `SendPingPacket` | `Ping` 패킷을 전송한다. |
| `SendEchoRequestPacket` | 문자열 echo 요청 패킷을 전송한다. |
| `SendOrderedPacket` | 순서 검증용 `TestPacketReq`를 전송한다. |
| `DisconnectClient` | 클라이언트 disconnect 흐름을 시작한다. |
| `WaitForConnected` | CONNECT ACK 처리로 연결 상태가 될 때까지 기다린다. |
| `WaitForPong` | `PONG` 패킷 수신을 기다린다. |
| `WaitForEcho` | 기대 문자열과 일치하는 echo 응답을 기다린다. |
| `WaitForOrderedResponse` | 기대 order와 일치하는 `TestPacketRes` 응답을 기다린다. |

테스트 클라이언트는 `ShouldSendConnectPacketOnStart`와 `ShouldSendReplyToServer`를 재정의한다. 기본 `RUDPClientCore`는 두 hook 모두 `true`를 반환하지만, `TestableRUDPClient`는 `reserve-timeout`과 `drop-ack` 시나리오를 만들기 위해 CONNECT 전송과 데이터 패킷 reply 전송을 제어한다. 기본 hook과 테스트 override의 차이는 [[Client/RUDPClientCoreHooks]]에 따로 정리한다.

---

## 문서 생성 후보 분류

docs-bot이 감지한 후보 중 아래 항목은 실제 함수 문서 대상이 아니다.

| 항목 | 분류 |
|------|------|
| `stream` | 지역 변수 선언 오탐 |
| `sizeof` | 표현식 오탐 |
| `lock` | `std::scoped_lock` 지역 변수 오탐 |
| `RUDPSession` | 생성자 initializer 오탐 |
| `mutableCommandLine` | 지역 변수 선언 오탐 |
| `impl` | 멤버 initializer 오탐 |

`FromPfxFile`은 `TLSHelper::ServerCertificateConfig`의 실제 public factory이므로 오탐이 아니다. 별도 테스트 문서가 아니라 [[Common/TLSHelper]]의 서버 인증서 설정 항목에서 다룬다.

`StartClient`, `StopClient`, `SetAutoReplyDataPackets`, `WaitUntil`처럼 선언부와 구현부 양쪽에서 잡힌 항목은 중복 후보로 병합해야 한다.
