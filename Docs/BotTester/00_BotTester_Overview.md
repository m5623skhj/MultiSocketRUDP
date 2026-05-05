# MultiSocketRUDPBotTester 개요

> [[MultiSocketRUDPCore|MultiSocketRUDP 서버]]의 부하 테스트 및 시나리오 검증 도구.  
> WPF 비주얼 노드 에디터에서 행동 트리(Behavior Tree)를 그래픽으로 설계하고,  
> AI(Gemini)에게 자연어로 요청해 자동 생성하거나, 직접 빌드해 봇으로 실행한다.

---

## 전체 아키텍처

![[BotTester_Architecture.svg]]

---

## 🗺️ 문서 탐색

### 봇 엔진
| 문서 | 내용 |
|------|------|
| [[BotActionGraph]] | 행동 트리 실행 엔진 |
| [[ActionNodes]] | 모든 노드 타입 레퍼런스 |
| [[RuntimeContext]] | 노드 간 데이터 공유 컨텍스트 |
| [[GraphValidator]] | 그래프 정합성 검증 |
| [[NodeExecutionStats]] | 노드 실행 통계 |

### UI / 비주얼 에디터
| 문서 | 내용 |
|------|------|
| [[BotActionGraphWindow]] | 메인 그래프 에디터 윈도우 |
| [[NodeConfigPanels]] | 노드별 설정 다이얼로그 |
| [[CanvasRenderer]] | 캔버스 렌더링·드래그·연결 |

### AI 연동
| 문서 | 내용 |
|------|------|
| [[AiTreeGenerator]] | Gemini AI 트리 자동 생성 |
| [[GeminiClient]] | Gemini API 클라이언트 |

### 클라이언트 코어 (C#)
| 문서 | 내용 |
|------|------|
| [[BotTesterCore]] | 세션 관리·봇 라이프사이클 |
| [[RudpSession_CS]] | C# RUDP 세션 구현 |
| [[SessionGetter_CS]] | TLS 세션 정보 수신 |

---

## 핵심 흐름

```
사용자
 ├─ [비주얼 에디터] 노드 드래그&드롭
 │    └─ BuildActionGraph() → ActionGraph
 │
 ├─ [AI Generator] 자연어 입력
 │    └─ Gemini API → JSON → AiNodeFactory → ActionGraph
 │
 └─ [Apply to BotTester]
      └─ BotTesterCore.SetBotActionGraph()
           └─ StartBotTest(N)
                └─ SessionBroker TLS → N개 Client 생성
                     └─ Client.OnConnected() → ActionGraph.TriggerEvent()
                          └─ 행동 트리 실행
```

---

## 주요 기술 스택

| 항목 | 내용 |
|------|------|
| UI 프레임워크 | WPF (.NET 9) |
| AI | Google Gemini (`gemini-2.5-flash`) |
| 암호화 | AES-GCM (`System.Security.Cryptography.AesGcm`) |
| 네트워크 | UDP (`UdpClient`) + TLS (`SslStream`) |
| 로깅 | Serilog (파일 롤링) |
| 패킷 정의 | [[PacketGenerator]]와 동기화 (`PacketDefine.yml`) |

---

## 문서 작성 기준

- `BotTester` 문서는 WPF UI, 봇 실행 엔진, AI 연동, C# 클라이언트 코어를 함께 다룬다.
- 생성자/소멸자는 특수한 경우가 아니면 함수 설명 대상으로 다루지 않는다.
- 함수 설명은 실제 코드 기준 시그니처와 호출 맥락을 우선한다.
- UI 문서에는 이벤트 핸들러와 내부 helper까지 포함하되, 공개 API와 내부 동작을 구분해 적는다.

---

## 관련 문서 (서버 연동)
- [[MultiSocketRUDPCore]] — 봇이 연결하는 서버
- [[RUDPSessionBroker]] — TLS 세션 발급
- [[PacketFormat]] — 공유 패킷 구조
