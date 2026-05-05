# MultiSocketRUDP & BotTester — Vault 개요

> 이 Vault는 **MultiSocketRUDP** (C++ 게임 서버 프레임워크) 와  
> **MultiSocketRUDPBotTester** (WPF 부하 테스트 도구) 의 기술 문서 모음이다.

---

## 빠른 진입점

| 목적 | 문서 |
|------|------|
| 처음 시작 | [[GettingStarted]] |
| 콘텐츠 서버 개발 | [[ContentServerGuide]] |
| 서버 구조 전체 파악 | [[MultiSocketRUDPCore]] |
| 세션 상속 및 API | [[RUDPSession]] |
| 패킷 흐름 이해 | [[PacketProcessing]] |
| 스레드 구조 이해 | [[ThreadModel]] |
| 연결 오류 해결 | [[Troubleshooting]] |
| 성능 최적화 | [[PerformanceTuning]] |
| 클라이언트 개발 | [[RUDPClientCore]] |
| BotTester | [[BotTester/00_BotTester_Overview]] |

---

## 문서 맵

![[Architecture_Overview.svg]]

---

## Server/

| 문서 | 핵심 내용 |
|------|-----------|
| [[MultiSocketRUDPCore]] | 서버 시작/종료 API, 세션 조회, 옵션 설정, 멀티소켓 구조 |
| [[RUDPSession]] | 상속 방법, 핸들러 등록, 송신 API, 이벤트 훅, 동시성 보호 |
| [[RUDPSessionBroker]] | TLS 세션 발급 흐름, 실패 처리, 인증서 설정 |
| [[RUDPSessionManager]] | 세션 풀 O(1) 할당/반환, 이중 반환 방지 |
| [[SessionLifecycle]] | 4상태 전이 다이어그램, 각 전이 조건 및 코드 |
| [[SessionComponents]] | StateMachine/CryptoContext/SocketContext/RIOContext/FlowManager |
| [[PacketProcessing]] | 수신 전체 파이프라인, PacketType 분기, 순서 보장, 이상 처리 |
| [[PacketFormat]] | 헤더/타입/시퀀스/페이로드/AuthTag 오프셋 |
| [[ThreadModel]] | IO Worker/Logic/Retransmission/Release/Heartbeat 스레드 상세 |
| [[RUDPIOHandler]] | DoRecv/DoSend/IOCompleted, MakeSendStream 배치 전송 |
| [[RIOManager]] | RIO 완료 큐 생성, 버퍼 등록, DequeueCompletions |
| [[RUDPPacketProcessor]] | ProcessByPacketType, TPS 카운터 |
| [[RUDPThreadManager]] | jthread 그룹 관리 |
| [[SendPacketInfo]] | 재전송 추적 구조체, RefCount, isErasedPacketInfo |
| [[Ticker]] | TimerEvent 주기 실행 |
| [[MemoryTracer]] | 메모리 누수 추적 |

## Client/

| 문서 | 핵심 내용 |
|------|-----------|
| [[RUDPClientCore]] | TLS 세션 수신, UDP 연결, 송신/수신 API, 흐름 제어 |
| [[ServerAliveChecker]] | 서버 무응답 감지, deadlock 방지 설계 |

## Common/

| 문서 | 핵심 내용 |
|------|-----------|
| [[CryptoSystem]] | AES-128-GCM 전체 구조, Nonce 레이아웃 |
| [[CryptoHelper]] | BCrypt 래퍼, thread_local 인스턴스 |
| [[PacketCryptoHelper]] | EncodePacket/DecodePacket, AAD 범위 |
| [[TLSHelper]] | SChannel TLS 1.2 핸드셰이크 |
| [[FlowController]] | CWND, RecvWindow, advertiseWindow |

## 가이드 문서

| 문서 | 핵심 내용 |
|------|-----------|
| [[ContentServerGuide]] | Step-by-Step 콘텐츠 서버 구현 |
| [[Troubleshooting]] | 연결/패킷/암호화/성능 문제 해결 |
| [[PerformanceTuning]] | 스레드/흐름제어/재전송 파라미터 최적화 |
| [[GettingStarted]] | 빠른 시작 (서버+BotTester) |
| [[Glossary]] | 용어집 |

## BotTester/

| 문서 | 핵심 내용 |
|------|-----------|
| [[BotTester/00_BotTester_Overview]] | 전체 구조, 시작 방법 |
| [[BotTester/Bot/BotActionGraph]] | TriggerType별 그래프, ActionGraphBuilder API |
| [[BotTester/Bot/ActionNodes]] | 전체 노드 레퍼런스 |
| [[BotTester/Bot/RuntimeContext]] | 공유 상태, 예약 키, 확장 메서드 |
| [[BotTester/Bot/GraphValidator]] | 유효성 검사, 순환 감지 |
| [[BotTester/UI/BotActionGraphWindow]] | 캔버스 에디터 UI |
| [[BotTester/AI/AiTreeGenerator]] | Gemini AI 7단계 흐름 |

---

## Diagrams/

![[Diagrams/README]]
---

## 검토 메모

- `[[Troubleshooting]]` 링크 표기로 통일하는 편이 안전하다.
- `Client` 영역에는 현재 `RUDPSessionBroker.md`도 포함되어 있으므로, 이 문서는 "클라이언트가 사용하는 브로커 프로토콜" 문서라는 점을 함께 보는 편이 맞다.
