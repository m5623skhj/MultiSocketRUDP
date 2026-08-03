# CI 가이드

> 변경 경로에 따라 실행되는 검사, PR 필수 체크, RTT 성능 이력 자동화의 운영 기준을 정리한다.

## 목차

1. [workflow 구성](#workflow-구성)
2. [변경 경로별 실행 범위](#변경-경로별-실행-범위)
3. [RTT 성능 벤치마크](#rtt-성능-벤치마크)
4. [필수 체크](#필수-체크)
5. [실패 조사 순서](#실패-조사-순서)
6. [더 자세히 보기](#더-자세히-보기)

---

## workflow 구성

| 파일 | 역할 |
|---|---|
| `.github/workflows/CI.yml` | 변경 경로 분류와 최종 상태 집계 |
| `.github/workflows/GoogleTest.yml` | C++ build, CoreTest, IntegrationTest, coverage |
| `.github/workflows/BotTester.yml` | .NET build, xUnit, C# protocol vector 검증 |
| `.github/workflows/RttBenchmark.yml` | Release RTT 측정, PR 결과 비교, `main` 공식 이력 갱신 |
| `.github/workflows/docs-bot.yml` | 문서 변경 후보 자동화 |
| `.github/workflows/GeminiPRCommoentBot.yml` | 보조 AI 리뷰 status |

---

## 변경 경로별 실행 범위

| 변경 경로 | Native GTest | BotTester | RTT Benchmark |
|---|---:|---:|---:|
| `MultiSocketRUDP/**`, C++ 테스트, submodule | 실행 | 미실행 | 실행 |
| `MultiSocketRUDPBotTester/**` | 미실행 | 실행 | 실행 |
| 공용 `ProtocolInteropVector.json` | 실행 | 실행 | 실행 |
| `Scripts/RTTBenchmark/**` | 미실행 | 미실행 | 실행 |
| `.github/workflows/CI.yml` | 실행 | 실행 | 미실행 |
| `.github/workflows/RttBenchmark.yml` | 미실행 | 미실행 | 실행 |
| 관련 없는 문서만 변경 | 미실행 | 미실행 | 미실행 |

경로 분류 결과는 테스트 면제를 뜻하지 않는다. 문서만 바뀌었더라도 문서가 설명하는 동작과 현재 코드가 일치하는지는 reviewer가 확인한다.

---

## RTT 성능 벤치마크

`RttBenchmark.yml`은 `ContentsServer`와 전용 BotTester 콘솔 클라이언트를 서로 다른 프로세스로 실행해 루프백 Ping/Pong RTT를 측정한다. 기능 정합성을 판정하는 기존 CI와 달리, 수정 전후의 지연 시간 추세를 관찰하기 위한 정보성 workflow다.

### 이벤트별 동작

| 이벤트 | 측정·표시 | 공식 이력 갱신 |
|---|---|---:|
| 관련 경로를 변경한 PR | Job Summary, PR 코멘트, 30일 artifact | 안 함 |
| `main` push | Job Summary, 30일 artifact | `benchmark-data` 브랜치에 자동 반영 |
| 수동 `workflow_dispatch` | Job Summary, 30일 artifact | 안 함 |
| `benchmark-data` push | workflow를 실행하지 않음 | 해당 없음 |

PR 결과는 `benchmark-data`의 직전 공식 측정과 비교한다. `main`에 병합된 뒤에는 별도 수정 없이 해당 commit의 측정을 다시 수행하고, 성공한 결과를 `rtt-history.json`에 기록한다. 같은 commit을 다시 측정하면 새 행을 추가하지 않고 기존 결과를 교체한다.

### 고정 측정 조건

| 항목 | 설정 |
|---|---|
| 서버 | MSVC x64 Release, `/O2` |
| IO worker | `RUDP_RTT_BENCHMARK_BUILD`로 `NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME` 강제 |
| BotTester | .NET 9 Release, `Optimize=true` |
| 유실률 0% | 워밍업 2,000회, 10,000회 × 5 runs |
| 유실률 10% | 워밍업 200회, 5,000회 × 5 runs |
| 유실 모델 | BotTester 송신과 수신에 각각 독립적으로 10% 적용 |
| 대표값 | 각 run 통계의 중앙값 |
| 실행 제한 | warmup과 각 run은 최대 300초, workflow는 최대 45분 |

0%와 10% 시나리오는 같은 runner에서 순차 실행한다. 병렬 측정은 CPU 경합으로 RTT를 왜곡하고 singleton BotTester 상태를 공유할 수 있으므로 사용하지 않는다.
각 warmup/run의 시작과 완료, P95/P99는 콘솔에 출력한다. 진행 로그가 300초 안에 완료되지 않으면 해당 run을 실패 처리하므로 무응답 상태로 전체 job 시간을 소비하지 않는다.

### 이력과 그래프 해석

- `benchmark-data/rtt-history.json`은 commit별 전체 공식 이력을 보존한다.
- `rtt-loss-0.svg`와 `rtt-loss-10.svg`는 최근 10개 공식 측정의 P95/P99를 표시한다.
- 그래프의 변화율은 현재 값과 이전 5개 측정 중앙값의 차이다. 양수면 RTT가 증가해 느려진 것이다.
- Job Summary와 PR 코멘트의 변화율은 직전 공식 측정과 비교한다.
- `Max`는 hosted runner의 OS 스케줄링 노이즈에 민감하므로 표의 참고값으로만 남기고 추세 그래프에는 사용하지 않는다.
- 저장소 `README.md`는 `benchmark-data` 브랜치의 raw SVG를 표시하므로 공식 이력이 갱신되면 별도 README commit 없이 그래프도 바뀐다.

GitHub-hosted runner는 전용 성능 장비가 아니므로 한 번의 상승만으로 회귀를 확정하지 않는다. P95/P99가 여러 commit에서 같은 방향으로 움직이는지 확인하고, 의심 commit은 수동 재실행 또는 동일 환경의 로컬 측정으로 교차 검증한다.

### 권한과 재귀 실행 방지

PR 측정 job은 저장소를 읽고 PR 코멘트를 갱신할 권한만 사용한다. `main` push 뒤의 이력 기록 job만 `contents: write`로 `benchmark-data`를 갱신한다. 이 push는 `GITHUB_TOKEN`으로 생성되고 workflow의 push 대상도 `main`으로 제한되어 있으므로 RTT workflow를 재귀 실행하지 않는다.

---

## 필수 체크

Branch protection의 필수 체크는 `build-and-test` 하나다. 이 job은 모든 PR에서 생성되며 필요한 workflow가 모두 성공하면 성공하고, 관련 없는 workflow의 정상 skip은 허용한다.

`RTT Benchmark`는 초기 운영 단계에서 성능 추세를 수집하는 정보성 검사이므로 `build-and-test`에 포함하지 않는다. 이후 merge gate로 승격하려면 runner 노이즈를 고려한 회귀 임계값을 먼저 합의하고 branch protection required check를 별도로 설정한다.

`Expected — Waiting for status to be reported`가 계속 보이면 repository required checks에 과거 이름이 남아 있는지 확인한다. `CI Gate`, `Native GTest`, `Native GTest / build-and-test`, `BotTester Protocol Interop` 같은 이전 항목이 아니라 `build-and-test`를 사용한다.

AI 리뷰 status `ai-review-check`는 외부 API의 rate limit과 일시 장애 영향을 받으므로 merge gate가 아닌 보조 신호로 취급한다.

---

## 실패 조사 순서

1. `build-and-test`가 요구한 하위 workflow를 확인한다.
2. build 실패와 test 실패를 분리한다.
3. test 실패면 XML과 실패 test 이름을 기준으로 로컬 단일 실행을 시도한다.
4. IntegrationTest면 인증서 생성, child process 종료, 포트·timeout 영향을 확인한다.
5. retry 성공만으로 종료하지 말고 최초 실패가 timing 의존인지 조사한다.

Native GTest 실행은 CoreTest 10분, IntegrationTest 15분을 상한으로 두며 전체 job은 45분으로 제한한다. 제한 시간을 넘기면 프로세스 트리를 종료하고 crash 항목으로 보고한다. IntegrationTest의 child process 출력은 pipe EOF를 무기한 기다리지 않고 현재 읽을 수 있는 데이터만 회수한다.

RTT workflow가 실패하면 다음 순서로 확인한다.

1. 서버가 Release x64, `/O2`, `RUDP_RTT_BENCHMARK_BUILD` 조건으로 빌드됐는지 확인한다.
2. 임시 인증서 생성과 정리, 서버 프로세스 종료, 사용 포트 충돌 여부를 확인한다.
3. `rtt-loss-0.json`과 `rtt-loss-10.json` 생성 여부를 artifact에서 확인한다.
4. PR 비교가 실패하면 `benchmark-data` 브랜치의 `rtt-history.json`을 읽을 수 있는지 확인한다.
5. `main` 측정은 성공했지만 이력 기록이 실패하면 workflow의 `contents: write` 권한과 branch 정책을 확인한다.

---

## 더 자세히 보기

- [테스트 상세 레퍼런스 — CI 흐름](TestingReference.md#ci-흐름)
- [통합·프로토콜 테스트](IntegrationTests.md)
- [RTT Benchmark 실행·결과 상세](../../Scripts/RTTBenchmark/README.md)
- [docs-bot](../../Scripts/DocsBot/README.md)
