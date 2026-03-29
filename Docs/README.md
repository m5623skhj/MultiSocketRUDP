# MultiSocketRUDP 문서 (Obsidian Vault)

> 이 폴더를 Obsidian에서 **Vault로 열기**하면 모든 링크와 다이어그램이 활성화됩니다.

---

## 📂 폴더 구조

```
MultiSocketRUDP-Docs/
├── 00_Overview.md         ← 전체 개요 & 문서 탐색 허브
├── GettingStarted.md      ← 콘텐츠 구현 빠른 시작
├── Glossary.md            ← 용어집
│
├── Server/                ← 서버 컴포넌트
│   ├── MultiSocketRUDPCore.md
│   ├── RUDPSession.md
│   ├── SessionLifecycle.md
│   ├── SessionComponents.md
│   ├── RUDPSessionBroker.md
│   ├── RUDPSessionManager.md
│   ├── PacketProcessing.md
│   ├── PacketFormat.md
│   ├── ThreadModel.md
│   ├── RUDPIOHandler.md
│   ├── RIOManager.md
│   ├── RUDPPacketProcessor.md
│   ├── RUDPThreadManager.md
│   ├── SendPacketInfo.md
│   ├── Ticker.md
│   └── MemoryTracer.md
│
├── Client/                ← 클라이언트 컴포넌트
│   ├── RUDPClientCore.md
│   └── ServerAliveChecker.md
│
├── Common/                ← 공통 모듈
│   ├── CryptoSystem.md
│   ├── CryptoHelper.md
│   ├── PacketCryptoHelper.md
│   ├── TLSHelper.md
│   └── FlowController.md
│
├── Logger/
│   └── Logger.md
│
├── Tools/
│   └── PacketGenerator.md
│
└── Diagrams/              ← SVG 다이어그램 (문서에서 인라인 표시)
    ├── Architecture_Overview.svg
    ├── Connection_Sequence.svg
    ├── SessionStateMachine.svg
    ├── ThreadModel.svg
    ├── PacketFlow.svg
    └── CryptoStructure.svg
```

---

## 🚀 시작점

- **처음 보는 경우** → [[00_Overview]]
- **구현 바로 시작** → [[GettingStarted]]
- **용어 모를 때** → [[Glossary]]
- **다이어그램만 보기** → [[Diagrams/README]]

---

## Obsidian 팁

- `Ctrl+G` (Graph View): 문서 간 링크 관계 시각화
- `Ctrl+Shift+F`: 전체 텍스트 검색
- `[[` 입력 후 자동완성으로 링크 탐색
- SVG는 `![[파일명.svg]]`로 인라인 렌더링됨

---

## 🤖 BotTester 서브 프로젝트

```
BotTester/
├── 00_BotTester_Overview.md   ← 진입점
├── Bot/
│   ├── BotActionGraph.md      ← 행동 트리 엔진
│   ├── ActionNodes.md         ← 전체 노드 레퍼런스
│   ├── RuntimeContext.md      ← 실행 컨텍스트
│   ├── GraphValidator.md      ← 검증기
│   └── NodeExecutionStats.md  ← 통계
├── UI/
│   ├── BotActionGraphWindow.md ← 메인 에디터
│   ├── CanvasRenderer.md       ← 렌더링·드래그·연결
│   └── NodeConfigPanels.md    ← 설정 다이얼로그
├── AI/
│   ├── AiTreeGenerator.md     ← AI 트리 자동 생성
│   └── GeminiClient.md        ← Gemini API 클라이언트
└── ClientCore/
    ├── BotTesterCore.md       ← 봇 세션 관리
    ├── RudpSession_CS.md      ← C# RUDP 세션
    └── SessionGetter_CS.md    ← TLS 세션 수신
```
