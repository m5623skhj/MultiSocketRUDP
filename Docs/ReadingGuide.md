# 문서 읽기 가이드

> 처음 방문한 사람이 필요한 문서만 선택해 읽도록 돕는 진입 가이드다.
> 전체 파일 목록이 필요하면 [문서 카탈로그](00_Overview.md)를 사용한다.

## 목차

1. [먼저 알아둘 점](#먼저-알아둘-점)
2. [시간별 권장 경로](#시간별-권장-경로)
3. [역할별 권장 경로](#역할별-권장-경로)
4. [증상별 빠른 찾기](#증상별-빠른-찾기)
5. [문서를 효율적으로 읽는 규칙](#문서를-효율적으로-읽는-규칙)
6. [문서 유형](#문서-유형)

---

## 먼저 알아둘 점

- `Docs/README.md`는 가장 짧은 진입점이다.
- 이 문서는 역할과 작업 목적에 따른 권장 읽기 순서를 제공한다.
- `00_Overview.md`는 전체 문서 카탈로그다.
- 클래스·컴포넌트 문서는 API와 구현 계약을 확인할 때 사용한다.
- `*Reference.md`는 여러 영역을 한 번에 추적할 때만 사용하는 상세 레퍼런스다.

GitHub에서는 이 문서와 허브 문서의 표준 Markdown 링크를 따라간다. Obsidian에서는 `Docs` 폴더를 Vault로 열면 기존 위키링크와 SVG 임베드도 사용할 수 있다.

---

## 시간별 권장 경로

### 10분: 프로젝트 성격만 파악

1. [전체 개요](00_Overview.md)
2. [빠른 시작](GettingStarted.md)의 실행 대상과 주의사항
3. [용어집](Glossary.md)에서 모르는 용어 확인

### 30분: 서버 데이터 흐름 파악

1. [MultiSocketRUDPCore](Server/MultiSocketRUDPCore.md)의 시작·종료 흐름
2. [세션 생명주기](Server/SessionLifecycle.md)의 상태 전이
3. [패킷 처리 파이프라인](Server/PacketProcessing.md)의 전체 다이어그램과 이상 패킷 매트릭스
4. [스레드 모델 허브](Server/ThreadModel.md)의 역할표와 동시성 주의사항

### 30분: 클라이언트 흐름 파악

1. [빠른 시작](GettingStarted.md)의 C++ 클라이언트 절차
2. [RUDPClientCore](Client/RUDPClientCore.md)의 연결 흐름과 공개 API
3. [ServerAliveChecker](Client/ServerAliveChecker.md)의 종료·deadlock 방지 규칙
4. [공통 패킷 포맷](Common/PacketFormat.md)과 [암호화 시스템](Common/CryptoSystem.md)

### 30분: BotTester 사용·확장

1. [BotTester 개요](BotTester/00_BotTester_Overview.md)
2. [Action 노드](BotTester/Bot/ActionNodes.md)
3. [RuntimeContext](BotTester/Bot/RuntimeContext.md)
4. 저장·복원이 필요하면 [GraphFileStorage](BotTester/Graph/GraphFileStorage.md)
5. AI 생성이 필요하면 [AiTreeGenerator](BotTester/AI/AiTreeGenerator.md)

---

## 역할별 권장 경로

| 역할 | 먼저 읽기 | 필요할 때 확장 |
|---|---|---|
| 콘텐츠 서버 개발자 | [GettingStarted](GettingStarted.md) → [ContentServerGuide](ContentServerGuide.md) | [RUDPSession](Server/RUDPSession.md), [PlayerManager](ContentServer/PlayerManager.md) |
| 코어 서버 개발자 | [MultiSocketRUDPCore](Server/MultiSocketRUDPCore.md) → [ThreadModel](Server/ThreadModel.md) | [RUDPIOHandler](Server/RUDPIOHandler.md), [RIOManager](Server/RIOManager.md), [SessionComponents](Server/SessionComponents.md) |
| 클라이언트 개발자 | [GettingStarted](GettingStarted.md) → [RUDPClientCore](Client/RUDPClientCore.md) | [ServerAliveChecker](Client/ServerAliveChecker.md), [FlowController](Common/FlowController.md) |
| 프로토콜·보안 검토자 | [PacketFormat](Common/PacketFormat.md) → [CryptoSystem](Common/CryptoSystem.md) | [PacketCryptoHelper](Common/PacketCryptoHelper.md), [TLSHelper](Common/TLSHelper.md), [서버 패킷 포맷](Server/PacketFormat.md) |
| 테스트 작성자 | [테스트 허브](Testing.md) | [유닛 테스트](Testing/UnitTests.md), [통합 테스트](Testing/IntegrationTests.md), [CI](Testing/CI.md) |
| 장애 대응자 | [Troubleshooting](Troubleshooting.md) | [PerformanceTuning](PerformanceTuning.md), 관련 컴포넌트 문서 |

---

## 증상별 빠른 찾기

| 증상 또는 질문 | 문서 |
|---|---|
| 세션이 연결되지 않는다 | [Troubleshooting](Troubleshooting.md), [SessionLifecycle](Server/SessionLifecycle.md) |
| 어떤 스레드가 값을 바꾸는지 모르겠다 | [ThreadModel](Server/ThreadModel.md), [LifecycleAndSynchronization](Server/Threading/LifecycleAndSynchronization.md) |
| 패킷이 핸들러까지 도달하지 않는다 | [PacketProcessing](Server/PacketProcessing.md), [RUDPPacketProcessor](Server/RUDPPacketProcessor.md) |
| ACK 또는 재전송이 이상하다 | [SendPacketInfo](Server/SendPacketInfo.md), [RetransmissionTimeoutEstimator](Server/RetransmissionTimeoutEstimator.md) |
| 암복호화가 C++/C#에서 다르다 | [CryptoSystem](Common/CryptoSystem.md), [IntegrationTests](Testing/IntegrationTests.md) |
| 테스트가 CI에서만 실패한다 | [Testing CI](Testing/CI.md), [IntegrationTests](Testing/IntegrationTests.md) |
| 옵션 값을 조정하고 싶다 | [PerformanceTuning](PerformanceTuning.md), 해당 컴포넌트의 옵션 섹션 |

---

## 문서를 효율적으로 읽는 규칙

1. 허브에서 시작하고 상세 레퍼런스는 마지막에 연다.
2. 처음에는 개요, 전제 조건, 실패 동작, 관련 문서만 읽는다.
3. 구현을 수정할 때만 함수별 코드 해설과 멤버 목록까지 내려간다.
4. 동시성 코드는 호출 스레드, 보호 수단, 객체 수명, 종료 순서를 한 묶음으로 확인한다.
5. 보안·프로토콜 문서는 설계 의도와 현재 코드의 실제 보장을 구분해 읽는다.
6. 같은 이름의 문서가 있는 `PacketFormat`과 `RUDPSessionBroker`는 `Common/`, `Server/`, `Client/` 경로까지 확인한다.

---

## 문서 유형

| 유형 | 목적 | 권장 분량 |
|---|---|---:|
| 허브 | 독자와 목적에 맞는 다음 문서 선택 | 150줄 이하 |
| 가이드 | 설치·구현·운영 절차 완수 | 한 작업 흐름 |
| 개념 문서 | 상태, 데이터 흐름, 설계 의도 설명 | 한 핵심 개념 |
| 컴포넌트 문서 | 공개 API, 전제 조건, 실패·동시성 계약 | 한 클래스 또는 밀접한 클래스군 |
| 상세 레퍼런스 | 여러 영역을 가로지르는 코드 추적 | 길이 제한 없음, 허브에서 직접 노출 최소화 |

문서 작성·분리 기준은 [문서 스타일 가이드](style_guide.md)의 “문서 분할과 탐색성” 절을 따른다.
