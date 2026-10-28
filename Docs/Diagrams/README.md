# 다이어그램 목록

> 이 폴더의 SVG 파일들은 Obsidian에서 `![[파일명.svg]]` 문법으로 인라인 표시됩니다.

| 파일 | 내용 | 사용 문서 |
|------|------|-----------|
| `Architecture_Overview.svg` | 전체 아키텍처 (클라이언트/서버/브로커) | [[00_Overview]] |
| `SessionStateMachine.svg` | 세션 상태 전이 다이어그램 | [[SessionLifecycle]] |
| `ThreadModel.svg` | 스레드 구조 및 데이터 흐름 | [[ThreadModel]] |
| `Connection_Sequence.svg` | TLS 세션 획득 + RUDP 연결 시퀀스 | [[00_Overview]] |
| `PacketFlow.svg` | 패킷 수신 처리 파이프라인 | [[PacketProcessing]] |
| `CryptoStructure.svg` | AES-GCM 암호화 구조 및 패킷 레이아웃 | [[CryptoSystem]] |

---

SVG의 설명·캡션은 한국어이며 코드 식별자는 유지한다. 그래프 편집 화면의 버튼 표기는 개념 설명용 번역이므로 실제 UI의 영문 이름은 해당 컴포넌트 문서에서 확인한다. `CryptoStructure.svg`는 PacketId가 평문·AAD 제외임을 표시하고, `PacketFlow.svg`는 AutoResetEvent와 서버 수신 map을 기준으로 한다.

## BotTester 다이어그램

| 파일 | 내용 | 사용 문서 |
|------|------|-----------|
| `BotTester_Architecture.svg` | BotTester 전체 아키텍처 | [[00_BotTester_Overview]] |
| `NodeHierarchy.svg` | 노드 타입 계층 구조 | [[ActionNodes]] |
| `AiTreeFlow.svg` | AI 트리 생성 7단계 흐름 | [[AiTreeGenerator]] |
| `GraphEditor_Layout.svg` | 에디터 UI 구성 및 포트 색상 | [[BotActionGraphWindow]] |
