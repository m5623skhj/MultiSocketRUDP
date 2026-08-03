# MultiSocketRUDP 문서

> GitHub와 Obsidian에서 함께 사용할 수 있는 문서 진입점이다.

처음 방문했다면 [문서 읽기 가이드](ReadingGuide.md)에서 역할·목적·시간에 맞는 경로를 선택한다. 전체 문서 목록은 [문서 카탈로그](00_Overview.md)에서 확인한다.

---

## 목적별 바로가기

| 목적 | 시작 문서 |
|---|---|
| 프로젝트를 처음 파악 | [문서 읽기 가이드](ReadingGuide.md) |
| 서버·클라이언트를 실행 | [빠른 시작](GettingStarted.md) |
| 콘텐츠 서버 구현 | [콘텐츠 서버 가이드](ContentServerGuide.md) |
| 서버 구조 이해 | [MultiSocketRUDPCore](Server/MultiSocketRUDPCore.md) |
| 패킷 흐름 추적 | [패킷 처리 파이프라인](Server/PacketProcessing.md) |
| 스레드·동시성 검토 | [스레드 모델 허브](Server/ThreadModel.md) |
| 치명 오류·프로세스 재시작 연동 | [치명 오류 통지와 프로세스 재시작](Server/FatalErrorHandling.md) |
| 테스트 선택·실행 | [테스트 허브](Testing.md) |
| 장애 조사 | [문제 해결](Troubleshooting.md) |
| 성능 조정 | [성능 튜닝](PerformanceTuning.md) |
| BotTester 사용 | [BotTester 개요](BotTester/00_BotTester_Overview.md) |

---

## 문서 탐색 구조

```text
README.md          짧은 진입점
  → ReadingGuide   역할·목적별 읽기 순서
  → 00_Overview    전체 문서 카탈로그
  → 허브 문서      테스트·스레드처럼 범위가 큰 주제의 선택 화면
  → 가이드/개념    작업 절차와 설계 흐름
  → 컴포넌트       API, 전제 조건, 실패·동시성 계약
  → *Reference     여러 영역을 한 번에 추적하는 상세 자료
```

허브와 진입 문서는 GitHub에서도 클릭 가능한 표준 Markdown 링크를 사용한다. 기술 문서의 `[[문서명]]`과 `![[다이어그램.svg]]`는 Obsidian용이다.

---

## 폴더 역할

| 폴더 | 내용 |
|---|---|
| `Server/` | 서버 코어, 세션, RIO, 패킷, thread |
| `Client/` | C++ 클라이언트와 연결 생존 감지 |
| `Common/` | 패킷 포맷, 암호화, TLS, 흐름 제어 |
| `Testing/` | 유닛·통합·CI와 상세 레퍼런스 |
| `BotTester/` | WPF 부하 테스트 도구와 행동 graph |
| `Tools/` | 패킷 생성·업로드와 개발 script |
| `ContentServer/` | 콘텐츠 서버 샘플 컴포넌트 |
| `Logger/` | 비동기 logger |
| `Diagrams/` | 문서에서 사용하는 SVG |

---

## Obsidian 사용

`Docs` 폴더를 Vault로 열면 위키링크, backlinks, graph view, SVG 임베드가 활성화된다.

- `Ctrl+Shift+F`: Vault 전체 검색
- `Ctrl+G`: 문서 관계 graph
- `[[`: 위키링크 자동 완성

문서를 추가하거나 나눌 때는 [문서 스타일 가이드](style_guide.md)의 분할·링크 규칙을 따른다.
