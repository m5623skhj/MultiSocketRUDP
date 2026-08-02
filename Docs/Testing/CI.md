# PR CI 가이드

> 변경 경로에 따라 실행되는 검사와 PR 필수 체크의 판정 기준을 정리한다.

## 목차

1. [workflow 구성](#workflow-구성)
2. [변경 경로별 실행 범위](#변경-경로별-실행-범위)
3. [필수 체크](#필수-체크)
4. [실패 조사 순서](#실패-조사-순서)
5. [더 자세히 보기](#더-자세히-보기)

---

## workflow 구성

| 파일 | 역할 |
|---|---|
| `.github/workflows/CI.yml` | 변경 경로 분류와 최종 상태 집계 |
| `.github/workflows/GoogleTest.yml` | C++ build, CoreTest, IntegrationTest, coverage |
| `.github/workflows/BotTester.yml` | .NET build, xUnit, C# protocol vector 검증 |
| `.github/workflows/docs-bot.yml` | 문서 변경 후보 자동화 |
| `.github/workflows/GeminiPRCommoentBot.yml` | 보조 AI 리뷰 status |

---

## 변경 경로별 실행 범위

| 변경 경로 | Native GTest | BotTester |
|---|---:|---:|
| `MultiSocketRUDP/**`, C++ 테스트, submodule | 실행 | 미실행 |
| `MultiSocketRUDPBotTester/**` | 미실행 | 실행 |
| 공용 `ProtocolInteropVector.json` | 실행 | 실행 |
| `CI.yml` | 실행 | 실행 |
| 관련 없는 문서만 변경 | 미실행 | 미실행 |

경로 분류 결과는 테스트 면제를 뜻하지 않는다. 문서만 바뀌었더라도 문서가 설명하는 동작과 현재 코드가 일치하는지는 reviewer가 확인한다.

---

## 필수 체크

Branch protection의 필수 체크는 `build-and-test` 하나다. 이 job은 모든 PR에서 생성되며 필요한 workflow가 모두 성공하면 성공하고, 관련 없는 workflow의 정상 skip은 허용한다.

`Expected — Waiting for status to be reported`가 계속 보이면 repository required checks에 과거 이름이 남아 있는지 확인한다. `CI Gate`, `Native GTest`, `Native GTest / build-and-test`, `BotTester Protocol Interop` 같은 이전 항목이 아니라 `build-and-test`를 사용한다.

AI 리뷰 status `ai-review-check`는 외부 API의 rate limit과 일시 장애 영향을 받으므로 merge gate가 아닌 보조 신호로 취급한다.

---

## 실패 조사 순서

1. `build-and-test`가 요구한 하위 workflow를 확인한다.
2. build 실패와 test 실패를 분리한다.
3. test 실패면 XML과 실패 test 이름을 기준으로 로컬 단일 실행을 시도한다.
4. IntegrationTest면 인증서 생성, child process 종료, 포트·timeout 영향을 확인한다.
5. retry 성공만으로 종료하지 말고 최초 실패가 timing 의존인지 조사한다.

---

## 더 자세히 보기

- [테스트 상세 레퍼런스 — CI 흐름](TestingReference.md#ci-흐름)
- [통합·프로토콜 테스트](IntegrationTests.md)
- [docs-bot](../../Scripts/DocsBot/README.md)
