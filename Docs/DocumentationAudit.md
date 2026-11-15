# 문서와 코드 대조 기록

> 2026-10-28 점검 기록이다. 기준 코드는 `0cfee60`이다. 이후 변경까지 자동 검증됐다는 뜻은 아니다.

## 목차

1. [범위와 방법](#범위와-방법)
2. [주요 정정](#주요-정정)
   - [재검토에서 추가 정정한 내용](#재검토에서-추가-정정한-내용)
3. [전체 문서와 대조 근거](#전체-문서와-대조-근거)
4. [남은 구현 한계](#남은-구현-한계)
5. [검증 범위](#검증-범위)

## 범위와 방법

추적 중인 프로젝트 Markdown 문서 82개와 SVG 10개를 대상으로 설명·API 이름·옵션·예제·동시성·실패 계약을 코드·프로젝트·테스트·워크플로와 대조했다. 동작이 맞는 문서는 불필요하게 다시 쓰지 않았다. 설명 문장은 한국어로 정리하고 코드 식별자·실행 명령·실제 UI 이름은 보존했다.

`logTemp/log_2026-04-27_17.md`는 과거 실행 결과 원문이므로 영어 로그 메시지·시각·데이터를 보존한다. 현재 동작·성능 기준선으로 사용하지 않는다. `external/CommonCode`, `CoreTest/subhook` 안의 외부 문서·라이선스는 별도 저장소의 원문으로 보존했다. CommonCode 직렬화 소스는 이 저장소 문서를 대조하는 데 사용했다. 옵션 `.txt`와 `requirements.txt`는 실행 입력으로 확인했으며 번역을 이유로 키·값·패키지명을 바꾸지 않았다.

## 주요 정정

- C++ 종료 요청·정리 스레드·검사기 join, 시작 취소·DNS·인증서 검증을 반영했다.
- 고정 UDP 포트·호스트 루프백·비신뢰성 큐 진단의 조건을 한글로 정리했다.
- PacketId의 암호화·인증 제외 범위를 본문과 SVG에 일치시켰다.
- BotTester v2 응답 오프셋·확장 스키마·수신 완료 대기·큐 종료 후 실패값을 정정했다.
- Logger 종료 우선 대기·flush, 세션 반환 절차, 서버 수신 보류 map을 정정했다.
- 외부 배포에 대한 미확인 보장·고정 성능 순위·테스트 수치 대신 근거 경로를 연결했다.

### 재검토에서 추가 정정한 내용

최초 검토 후 상세 예제와 중복 설명을 다시 대조하여 아래 내용을 정정했다. 링크·XML 검사만으로 동작 설명의 정확성을 확정하지 않았다.

| 항목 | 정정 내용 |
|---|---|
| [ACK 규격](Common/PacketFormat.md#send_reply_type-0x04--ack) | `AdvertiseWindow`는 서버→클라이언트에만 있다. 반대 방향과 하트비트 응답의 본문·전체 길이를 구분했다. CONNECT의 본문 오프셋도 공통 Sequence와 분리했다. |
| [송신 중복 검사](Server/Session/SessionComponentsReference.md#6-sessionsendcontext) | 키의 `isUnreliable`을 복원하고, ACK 맵은 삽입 성공 시에만 참조를 추가하도록 예제를 맞췄다. |
| [세션 연결](Server/SessionLifecycle.md#4-전이-2-reserved--connected) | 같은 클라이언트의 중복 CONNECT에는 ACK만 재전송한다. 최초 연결에서 순서 관리자도 초기화하며 훅·연결 수는 중복 갱신하지 않는다. |
| [AI 가져오기](BotTester/AI/AiTreeGenerator.md#가져오기-제한) | 조건식 해석, Assert 조건, PacketParser Setter, RandomChoice 등의 지원 한계를 명시하고 실행 가능한 JSON 구조와 코드 구성 예제를 구분했다. |
| [노드 통계](BotTester/Bot/NodeExecutionStats.md#수집-시점) | 동기 후속 체인 시간·예외의 부모 포함, 이름별 집계와 비동기 완료 측정의 한계를 명시했다. |
| [그래프 검증](BotTester/Bot/GraphValidator.md#노드별-검증-규칙) | 스키마가 있는 송신의 허용 조건과 timeout·반복·가중치·변수값 등 누락된 검증 규칙을 반영했다. |
| [C++ 상세 예제](Server/PacketProcessing.md#10-ack-전송-sendreplytoclient) | ACK는 송신 작업 guard 아래 `SendPacketImmediate()`로 전송한다. RIO 정리·초기화의 필수 인자와 흐름 제어 잠금 계약을 보완했다. |
| [브로커 길이 필드](BotTester/ClientCore/SessionGetter_CS.md#bottestercore에서의-사용) | 길이 필드 시작 오프셋 주석을 `PayloadPosition = 1`에 맞췄다. |
| [성능 그래프](Testing/CI.md#이력과-그래프-해석) | RTT 그래프의 이전 최대 5개 중앙값과 채널 그래프의 직전 호환 측정값 비교를 구분했다. |
| 연관 설명 | [재전송 시간](BotTester/ClientCore/BufferStore.md)은 UTC Unix 밀리초이며 단조 시계가 아님을 명시했다. SVG의 수신 번호 증가와 콜백 순서, 직렬화 목차 앵커도 정정했다. |

## 전체 문서와 대조 근거

아래 목록은 점검한 82개 문서를 모두 포함한다. 각 그룹의 근거는 주요 확인 지점이며 연관 헤더·구현·테스트도 함께 대조한다. 같은 이름의 서버·클라이언트 문서는 링크 경로로 구분한다.

### 서버 코어·세션·스레드

대조 근거: [MultiSocketRUDPCore.cpp](../MultiSocketRUDP/MultiSocketRUDPServer/MultiSocketRUDPCore.cpp) · [MultiSocketRUDPCore.Workers.cpp](../MultiSocketRUDP/MultiSocketRUDPServer/MultiSocketRUDPCore.Workers.cpp) · [RUDPSession.cpp](../MultiSocketRUDP/MultiSocketRUDPServer/RUDPSession.cpp) · [SessionSendContext.cpp](../MultiSocketRUDP/MultiSocketRUDPServer/SessionSendContext.cpp) · [SessionPacketOrderer.cpp](../MultiSocketRUDP/MultiSocketRUDPServer/SessionPacketOrderer.cpp) · [NetBufferContainerOperators.inl](../external/CommonCode/Common/NetBufferContainerOperators.inl)

- [Docs/Server/ContainerSerialization.md](Server/ContainerSerialization.md)
- [Docs/Server/FatalErrorHandling.md](Server/FatalErrorHandling.md)
- [Docs/Server/MemoryTracer.md](Server/MemoryTracer.md)
- [Docs/Server/MultiSocketRUDPCore.md](Server/MultiSocketRUDPCore.md)
- [Docs/Server/PacketFormat.md](Server/PacketFormat.md)
- [Docs/Server/PacketProcessing.md](Server/PacketProcessing.md)
- [Docs/Server/RIOManager.md](Server/RIOManager.md)
- [Docs/Server/RUDPIOHandler.md](Server/RUDPIOHandler.md)
- [Docs/Server/RUDPPacketProcessor.md](Server/RUDPPacketProcessor.md)
- [Docs/Server/RUDPSession.md](Server/RUDPSession.md)
- [Docs/Server/RUDPSessionBroker.md](Server/RUDPSessionBroker.md)
- [Docs/Server/RUDPSessionManager.md](Server/RUDPSessionManager.md)
- [Docs/Server/RUDPThreadManager.md](Server/RUDPThreadManager.md)
- [Docs/Server/RetransmissionTimeoutEstimator.md](Server/RetransmissionTimeoutEstimator.md)
- [Docs/Server/SendPacketInfo.md](Server/SendPacketInfo.md)
- [Docs/Server/Session/ReceiveContext.md](Server/Session/ReceiveContext.md)
- [Docs/Server/Session/SendAndFlow.md](Server/Session/SendAndFlow.md)
- [Docs/Server/Session/SessionComponentsReference.md](Server/Session/SessionComponentsReference.md)
- [Docs/Server/Session/StateCryptoAndSocket.md](Server/Session/StateCryptoAndSocket.md)
- [Docs/Server/SessionComponents.md](Server/SessionComponents.md)
- [Docs/Server/SessionLifecycle.md](Server/SessionLifecycle.md)
- [Docs/Server/ThreadModel.md](Server/ThreadModel.md)
- [Docs/Server/Threading/LifecycleAndSynchronization.md](Server/Threading/LifecycleAndSynchronization.md)
- [Docs/Server/Threading/ThreadModelReference.md](Server/Threading/ThreadModelReference.md)
- [Docs/Server/Threading/WorkerThreads.md](Server/Threading/WorkerThreads.md)
- [Docs/Server/Ticker.md](Server/Ticker.md)

### C++ 클라이언트

대조 근거: [RUDPClientCore.h](../MultiSocketRUDP/MultiSocketRUDPClient/RUDPClientCore.h) · [RUDPClientCore.cpp](../MultiSocketRUDP/MultiSocketRUDPClient/RUDPClientCore.cpp) · [SessionGetter.cpp](../MultiSocketRUDP/MultiSocketRUDPClient/SessionGetter.cpp) · [ServerAliveChecker.cpp](../MultiSocketRUDP/MultiSocketRUDPClient/ServerAliveChecker.cpp)

- [Docs/Client/RUDPClientCore.md](Client/RUDPClientCore.md)
- [Docs/Client/RUDPClientCoreHooks.md](Client/RUDPClientCoreHooks.md)
- [Docs/Client/RUDPSessionBroker.md](Client/RUDPSessionBroker.md)
- [Docs/Client/ServerAliveChecker.md](Client/ServerAliveChecker.md)

### 공통 패킷·암호화·TLS

대조 근거: [PacketCryptoHelper.h](../MultiSocketRUDP/Common/PacketCrypto/PacketCryptoHelper.h) · [CryptoHelper.cpp](../MultiSocketRUDP/Common/Crypto/CryptoHelper.cpp) · [TLSHelper.h](../MultiSocketRUDP/Common/TLS/TLSHelper.h) · [TLSHelperClient.cpp](../MultiSocketRUDP/Common/TLS/TLSHelperClient.cpp) · [TLSHelperServer.cpp](../MultiSocketRUDP/Common/TLS/TLSHelperServer.cpp) · [RUDPFlowManager.h](../MultiSocketRUDP/Common/FlowController/RUDPFlowManager.h)

- [Docs/Common/CryptoHelper.md](Common/CryptoHelper.md)
- [Docs/Common/CryptoSystem.md](Common/CryptoSystem.md)
- [Docs/Common/FlowController.md](Common/FlowController.md)
- [Docs/Common/PacketCryptoHelper.md](Common/PacketCryptoHelper.md)
- [Docs/Common/PacketFormat.md](Common/PacketFormat.md)
- [Docs/Common/TLSHelper.md](Common/TLSHelper.md)

### BotTester 엔진

대조 근거: [ActionGraph.cs](../MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/Bot/ActionGraph.cs) · [VariableNodes.cs](../MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/Bot/VariableNodes.cs) · [PacketSchema.cs](../MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/Bot/PacketSchema.cs) · [NodeExecutionStats.cs](../MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/Bot/NodeExecutionStats.cs) · [NodeExecutionHelper.cs](../MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/Bot/NodeExecutionHelper.cs) · [GraphValidator.cs](../MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/Bot/GraphValidator.cs)

- [Docs/BotTester/Bot/ActionNodes.md](BotTester/Bot/ActionNodes.md)
- [Docs/BotTester/Bot/BotActionGraph.md](BotTester/Bot/BotActionGraph.md)
- [Docs/BotTester/Bot/GraphValidator.md](BotTester/Bot/GraphValidator.md)
- [Docs/BotTester/Bot/NodeExecutionStats.md](BotTester/Bot/NodeExecutionStats.md)
- [Docs/BotTester/Bot/PacketSchema.md](BotTester/Bot/PacketSchema.md)
- [Docs/BotTester/Bot/RuntimeContext.md](BotTester/Bot/RuntimeContext.md)

### BotTester 네트워크

대조 근거: [BotTesterCore.cs](../MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/ClientCore/BotTesterCore.cs) · [RUDPSession.cs](../MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/ClientCore/RUDPSession.cs) · [SessionGetter.cs](../MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/ClientCore/SessionGetter.cs) · [BufferStore.cs](../MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/ClientCore/BufferStore.cs) · [UnreliablePacketQueue.cs](../MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/ClientCore/UnreliablePacketQueue.cs)

- [Docs/BotTester/ClientCore/BotTesterCore.md](BotTester/ClientCore/BotTesterCore.md)
- [Docs/BotTester/ClientCore/BufferStore.md](BotTester/ClientCore/BufferStore.md)
- [Docs/BotTester/ClientCore/PacketLossSimulator.md](BotTester/ClientCore/PacketLossSimulator.md)
- [Docs/BotTester/ClientCore/RudpSession_CS.md](BotTester/ClientCore/RudpSession_CS.md)
- [Docs/BotTester/ClientCore/SessionGetter_CS.md](BotTester/ClientCore/SessionGetter_CS.md)

### BotTester 화면·저장

대조 근거: [BotActionGraphWindow.xaml.cs](../MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/BotActionGraphWindow.xaml.cs) · [NodeCanvasRenderer.cs](../MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/CanvasRenderer/NodeCanvasRenderer.cs) · [NodeConfigPanels.cs](../MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/UI/NodeConfigPanels.cs)

- [Docs/BotTester/UI/BotActionGraphWindow.md](BotTester/UI/BotActionGraphWindow.md)
- [Docs/BotTester/UI/CanvasRenderer.md](BotTester/UI/CanvasRenderer.md)
- [Docs/BotTester/UI/NodeConfigPanels.md](BotTester/UI/NodeConfigPanels.md)

### BotTester AI

대조 근거: [AiTreeGenerator.cs](../MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/AiTreeGenerator.cs) · [AiTreeService.cs](../MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/AI/AiTreeService.cs) · [AiNodeFactory.cs](../MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/AI/AiNodeFactory.cs) · [GeminiClient.cs](../MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/WithGeminiClient/GeminiClient.cs)

- [Docs/BotTester/AI/AiTreeGenerator.md](BotTester/AI/AiTreeGenerator.md)
- [Docs/BotTester/AI/GeminiClient.md](BotTester/AI/GeminiClient.md)

### BotTester 그래프 저장

대조 근거: [GraphFileStorage.cs](../MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/Graph/GraphFileStorage.cs) · [GraphFileModels.cs](../MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/Graph/GraphFileModels.cs)

- [Docs/BotTester/Graph/GraphFileStorage.md](BotTester/Graph/GraphFileStorage.md)

### 도구

대조 근거: [PacketGenerator.py](../MultiSocketRUDP/Tool/PacketGenerator/PacketGenerator.py) · [PacketSchema.py](../MultiSocketRUDP/Tool/PacketGenerator/PacketSchema.py) · [PacketUpload.py](../MultiSocketRUDP/Tool/PacketUploader/PacketUpload.py) · [CreateDevTLSCert.bat](../MultiSocketRUDP/Tool/ForTLS/CreateDevTLSCert.bat) · [LogCompress.bat](../MultiSocketRUDP/Tool/LogCompress.bat)

- [Docs/Tools/DevelopmentScripts.md](Tools/DevelopmentScripts.md)
- [Docs/Tools/PacketGenerator.md](Tools/PacketGenerator.md)
- [Docs/Tools/PacketUploader.md](Tools/PacketUploader.md)

### 테스트와 CI

대조 근거: [CoreTest.vcxproj](../MultiSocketRUDP/CoreTest/CoreTest.vcxproj) · [IntegrationTest.cpp](../MultiSocketRUDP/IntegrationTest/IntegrationTest.cpp) · [CI.yml](../.github/workflows/CI.yml) · [GoogleTest.yml](../.github/workflows/GoogleTest.yml) · [BotTester.yml](../.github/workflows/BotTester.yml) · [RttBenchmark.yml](../.github/workflows/RttBenchmark.yml)

- [Docs/Testing.md](Testing.md)
- [Docs/Testing/CI.md](Testing/CI.md)
- [Docs/Testing/IntegrationTests.md](Testing/IntegrationTests.md)
- [Docs/Testing/TestingReference.md](Testing/TestingReference.md)
- [Docs/Testing/UnitTests.md](Testing/UnitTests.md)

### Logger

대조 근거: [Logger.cpp](../MultiSocketRUDP/Logger/Logger.cpp) · [LogClass.h](../MultiSocketRUDP/Logger/LogClass.h)

- [Docs/Logger/Logger.md](Logger/Logger.md)

### 콘텐츠 컴포넌트

대조 근거: [PlayerManager.cpp](../MultiSocketRUDP/ContentsServer/PlayerManager.cpp) · [Player.cpp](../MultiSocketRUDP/ContentsServer/Player.cpp)

- [Docs/ContentServer/PlayerManager.md](ContentServer/PlayerManager.md)

### 문서 자동화

대조 근거: [main.py](../Scripts/DocsBot/main.py) · [config.py](../Scripts/DocsBot/config.py) · [doc_mapper.py](../Scripts/DocsBot/doc_mapper.py) · [doc_writer.py](../Scripts/DocsBot/doc_writer.py) · [ai_client.py](../Scripts/DocsBot/ai_client.py) · [docs-bot.yml](../.github/workflows/docs-bot.yml)

- [Scripts/DocsBot/README.md](../Scripts/DocsBot/README.md)

### ASan

대조 근거: [Invoke-ASan.ps1](../Scripts/ASan/Invoke-ASan.ps1) · [AddressSanitizer.yml](../.github/workflows/AddressSanitizer.yml)

- [Scripts/ASan/README.md](../Scripts/ASan/README.md)

### 안정성 반복 검사

대조 근거: [Invoke-StabilityTests.ps1](../Scripts/Stability/Invoke-StabilityTests.ps1) · [Test-StabilityRunner.ps1](../Scripts/Stability/Test-StabilityRunner.ps1) · [Stability.yml](../.github/workflows/Stability.yml)

- [Scripts/Stability/README.md](../Scripts/Stability/README.md)

### RTT 자동화

대조 근거: [Invoke-RttBenchmark.ps1](../Scripts/RTTBenchmark/Invoke-RttBenchmark.ps1) · [update_rtt_history.py](../Scripts/RTTBenchmark/update_rtt_history.py) · [channel_history.py](../Scripts/RTTBenchmark/channel_history.py) · [RttBenchmark.yml](../.github/workflows/RttBenchmark.yml)

- [Scripts/RTTBenchmark/README.md](../Scripts/RTTBenchmark/README.md)

### 개요·가이드·용어·다이어그램 안내

대조 근거: [MultiSocketRUDP.sln](../MultiSocketRUDP/MultiSocketRUDP.sln) · [main.cpp](../MultiSocketRUDP/ContentsServer/main.cpp) · [main.cpp](../MultiSocketRUDP/ContentsClient/main.cpp) · [CoreConstants.h](../MultiSocketRUDP/Common/etc/CoreConstants.h) · [MultiSocketRUDPBotTester.csproj](../MultiSocketRUDPBotTester/MultiSocketRUDPBotTester/MultiSocketRUDPBotTester.csproj) · [RttStressBenchmarkOptions.cs](../MultiSocketRUDPBotTester/MultiSocketRUDPBotTester.RttBenchmark/RttStressBenchmarkOptions.cs) · [style_guide.md](style_guide.md)

- [Docs/00_Overview.md](00_Overview.md)
- [Docs/BotTester/00_BotTester_Overview.md](BotTester/00_BotTester_Overview.md)
- [Docs/ContentServerGuide.md](ContentServerGuide.md)
- [Docs/Diagrams/README.md](Diagrams/README.md)
- [Docs/GettingStarted.md](GettingStarted.md)
- [Docs/Glossary.md](Glossary.md)
- [Docs/PerformanceTuning.md](PerformanceTuning.md)
- [Docs/README.md](README.md)
- [Docs/RTT_TEST_COMPARISON_CHECKLIST_KR.md](RTT_TEST_COMPARISON_CHECKLIST_KR.md)
- [Docs/ReadingGuide.md](ReadingGuide.md)
- [Docs/Troubleshooting.md](Troubleshooting.md)
- [Docs/UnreliableChannel.md](UnreliableChannel.md)
- [Docs/style_guide.md](style_guide.md)
- [MultiSocketRUDPBotTester/README.md](../MultiSocketRUDPBotTester/README.md)
- [README.md](../README.md)

SVG 10개는 대응 문서의 코드 경로와 대조했다. 암호화 범위·헤더 5바이트, 수신 map·AutoResetEvent, 재전송 검사 16ms·수신 완료 대기, 기본/RTT 빌드 sleep 모드를 반영했다.

## 남은 구현 한계

| 항목 | 현재 구현과 영향 |
|---|---|
| 일반 패킷 ID | C++·C# 모두 PacketId가 암호화·AAD 밖이다. 본문 인증으로 ID 무결성을 보장하지 않는다. |
| 테스트 인증서 | 기존 배치는 DevServerCert PFX만 준비한다. 새 C++ 인증 정책의 신뢰 체인·접속 이름 SAN은 별도 준비가 필요하다. |
| C# 브로커 인증 | 기본 지문 없는 호출은 검증을 생략하고 지문 경로도 해시 일치만 검사한다. |
| C# Debug 로그 | BotTesterCore가 키·솔트를 포함하는 브로커 응답 바이트를 기록한다. 해당 로그의 외부 공유·운영 수집에 주의해야 한다. |
| 온라인 폐기 확인 | C++은 체인·이름을 확인하지만 CRL·OCSP 온라인 확인을 요청하지 않는다. |
| 번호 소진 | 64비트 번호 순환 방어가 없으므로 nonce 재사용 전에 키를 교체해야 한다. |
| 자동 문서 도구 | DocsBot의 기존 요약 오류·매핑 한계는 Scripts/DocsBot/README.md에 유지한다. 이번 작업에서 실행·수정하지 않았다. |

이 항목들은 문서에 드러낸 기존 제한이다. 동작 코드·옵션 파일·CI·인증서 저장소는 변경하지 않았다.

## 검증 범위

수정 후 새 대조 기록을 포함한 Markdown 83개에서 로컬 링크·앵커 716개와 위키링크 312개의 대상을 검사했으며 누락·모호한 대상은 없었다. 코드 펜스 540쌍과 SVG 10개의 XML 구문도 확인했다. 노드 문서의 JSON 예제 12개는 구문 검사를 통과했고, 자식까지 포함한 20개 노드 타입이 현재 AI 검증기의 허용 목록에 포함됨을 확인했다. 이는 UI 가져오기나 네트워크 실행 테스트를 대신하지 않는다.

코드 식별자·실제 로그 문자열을 제외한 독립 영어 설명 문장 검사에서 잔존 항목은 없었다. 공통 상수·생성 스키마·워크플로 조건은 대응 소스와 대조했다. 위 추가 정정 항목은 관련 상세 문서·예제·구현을 다시 대조했으며, 이 정적 검토 범위에서 추가 불일치는 발견하지 못했다.

문서 전용 변경이므로 C++/.NET 빌드, 네트워크 통합 테스트, ASan, 성능 재측정은 수행하지 않았다. 링크·XML 검사 통과는 런타임 동작이나 보안 문제 해결을 뜻하지 않는다.
