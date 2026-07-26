# docs-bot

> **머지된 PR의 인터페이스 변경을 감지해 기능 문서 수정 PR을 제안하는 GitHub Actions 파이프라인이다.**
> 완전 자동 문서 생성기가 아니라, 변경 누락을 찾고 사람이 검토할 초안을 만드는 보조 도구다.

---

## 목차

1. [자동화 범위](#1-자동화-범위)
2. [실행 조건과 환경 변수](#2-실행-조건과-환경-변수)
3. [파이프라인](#3-파이프라인)
4. [모듈](#4-모듈)
5. [매핑과 문서 수정 규칙](#5-매핑과-문서-수정-규칙)
6. [AI provider와 재시도](#6-ai-provider와-재시도)
7. [출력과 PR](#7-출력과-pr)
8. [한계와 수동 검토](#8-한계와-수동-검토)

---

## 1. 자동화 범위

대상은 `.h`, `.hpp`, `.cpp`, `.cc`, `.cs`의 함수 인터페이스 변경이다.

- 함수 시그니처, 파라미터, 반환형, 접근 제한자, 한정자, 인접 문서 주석을 추출한다.
- C++는 header 변경을 우선하며 `.cpp`만 바뀐 내부 구현 변경은 필터링한다.
- 생성자와 소멸자는 기본적으로 제외한다.
- 기존 문서의 함수 섹션 비교, 삭제 섹션 제거, 문서 누락 후보 보고를 수행한다.
- `EXCLUDED_DOCS`에 등록한 아키텍처·가이드 문서는 자동 수정하지 않고 수동 검토 대상으로 표시한다.

새 클래스에 대응하는 문서 파일이 없으면 실제 파일을 자동 생성하지 않는다. PR 본문에 클래스와 함수 시그니처 기반 skeleton을 표시해 생성 여부를 사람이 결정하게 한다.

---

## 2. 실행 조건과 환경 변수

workflow: `.github/workflows/docs-bot.yml`

| trigger | 조건 |
|---------|------|
| schedule | 매일 `00:00 UTC` |
| pull request label | `docs-review` 라벨이 추가됨 |
| manual | `workflow_dispatch` |

필수 환경 변수:

| 변수 | 설명 |
|------|------|
| `GITHUB_TOKEN` | GitHub API tree, commit, PR 작업 |
| `REPO` | `owner/repository` 형식 |
| `DOCS_BOT_LAST_RUN` | 이전 성공 처리 기준 UTC 시각 |
| `AI_PROVIDER` | `claude`, `openai`, `gemini` 중 하나 |
| `AI_API_KEY` | 선택 provider의 API key |
| `AI_MODEL` | 선택 사항. 비어 있으면 provider 기본 모델 사용 |

workflow는 repository variable을 읽고 갱신할 때 `PAT` secret을 `GH_TOKEN`으로 사용한다.

---

## 3. 파이프라인

```text
1. DOCS_BOT_LAST_RUN 파싱
   └─ 값이 없으면 current_time output을 기록하고 종료

2. 기준 시각 이후 머지된 PR 수집
   └─ docs-bot 라벨 PR 제외

3. 열려 있는 이전 docs-bot PR 닫기

4. 변경 감지와 코드 추출
   ├─ 대상 확장자 파일의 main 최종 내용 로드
   ├─ interface 변경 필터링
   └─ tree-sitter 우선, 정규식 fallback

5. 문서 파일과 섹션 매핑
   ├─ 수동 override
   ├─ 코드 디렉터리 → 문서 디렉터리
   └─ 전체 Docs fallback

6. AI 분석
   ├─ 기존 섹션과 현재 함수 비교
   └─ 문서 없는 신규 함수 초안 생성

7. 문서 변경 계산
   ├─ 기존 섹션 교체
   ├─ 삭제 함수 섹션 제거
   ├─ 목차 갱신
   └─ 위키링크와 frontmatter 보존 검사

8. 변경이 있으면 branch → commit → PR 생성

9. workflow가 PR 생성 성공 시 DOCS_BOT_LAST_RUN 갱신
```

AI provider나 API key가 없으면 변경 후보 요약을 출력하고 `ai_skipped=true`로 정상 종료하는 것이 의도된 흐름이다. 이 경우 기준 시각을 갱신하지 않아 설정을 추가한 뒤 같은 변경을 다시 처리할 수 있다. 다만 현재 `_print_summary()`에는 아래 한계가 있어 삭제 매핑이나 제외 문서 매핑이 있으면 예외로 종료될 수 있다.

---

## 4. 모듈

| 파일 | 역할 |
|------|------|
| `main.py` | 전체 파이프라인과 GitHub Actions output 조율 |
| `config.py` | 경로 매핑, 제외 문서, provider, branch·label 상수 |
| `git_utils.py` | PR 조회, main 파일 읽기, branch·commit·PR API |
| `change_detector.py` | unified diff와 최종 파일을 이용한 함수 변경 감지 |
| `code_extractor.py` | tree-sitter/정규식 기반 `FunctionInfo` 추출 |
| `doc_mapper.py` | 클래스→문서, 함수→섹션 매핑 및 그룹화 |
| `ai_client.py` | provider별 호출과 비교·생성 prompt |
| `doc_writer.py` | 섹션 교체·삭제, 목차, PR 본문 생성 |
| `requirements.txt` | Python 의존성 |
| `__init__.py` | package marker |

---

## 5. 매핑과 문서 수정 규칙

### 문서 파일 매핑

1. `CLASS_TO_DOC_OVERRIDE`에서 소문자 클래스명을 찾는다.
2. `CODE_TO_DOCS_DIR_MAP`으로 코드 경로를 문서 디렉터리에 대응시킨 뒤 `{ClassName}.md`를 찾는다.
3. 전체 `Docs/` 파일명 index에서 같은 클래스명을 찾는다.

### 섹션 매핑

1. heading에 함수명이 포함된 섹션
2. 코드 블록에 함수 시그니처가 포함된 섹션
3. 본문에 함수명이 언급된 섹션

가장 높은 우선순위의 결과만 사용하고 최대 3개 섹션으로 제한한다. 같은 문서·같은 시작 줄의 변경은 한 AI 요청으로 묶는다.

### 변경 적용

- AI가 `needs_update=true`로 판정한 섹션만 교체한다.
- 삭제 함수는 매핑된 섹션과 앞쪽 구분선을 제거한다.
- 추가 초안은 대응 문서 경로가 있을 때만 `## 관련 문서` 앞에 넣을 수 있다.
- 번호가 있는 `## N. 제목` 형식의 목차만 자동 갱신한다.
- 기존 위키링크가 사라지면 warning을 남긴다.
- 기존 YAML frontmatter와 파일 끝 newline을 보존한다.

스타일 입력은 `Docs/style_guide.md`를 사용한다.

---

## 6. AI provider와 재시도

현재 기본 모델은 아래와 같다.

| provider | 기본 모델 | endpoint 방식 |
|----------|-----------|---------------|
| `claude` | `claude-sonnet-4-20250514` | Anthropic Messages API |
| `openai` | `gpt-4o` | Chat Completions API |
| `gemini` | `gemini-2.5-flash` | Gemini `generateContent` API |

모든 provider는 요청 timeout `60초`를 사용한다. Claude와 OpenAI 요청은 최대 출력 `4096` token을 지정하고, Gemini 요청은 별도 출력 한도를 전달하지 않는다. 공통 재시도 정책은 아래와 같다.

- rate limit: 2초, 4초, 8초 지수 backoff로 최대 3회
- timeout: 1회 재시도
- server error: 최대 2회 재시도
- JSON 파싱 실패: `is_error=true`로 표시하고 문서에 적용하지 않음

---

## 7. 출력과 PR

GitHub Actions output은 `$GITHUB_OUTPUT`에 기록한다. 로컬 실행에서는 `[OUTPUT] key=value` 형식으로 출력한다.

| output | 의미 |
|--------|------|
| `initial_run` | 기준 시각이 없어 초기화만 필요함 |
| `current_time` | 초기 기준으로 저장할 UTC 시각 |
| `has_changes` | 인터페이스 또는 문서 변경 후보 존재 여부 |
| `ai_skipped` | AI 설정 부족으로 분석을 생략함 |
| `pr_number` | 생성된 문서 PR 번호 |

branch는 `docs-bot/YYYYMMDD-HHMMSS`, 제목과 commit message는 `[docs-bot] YYYY-MM-DD 문서 자동 갱신` 형식이다. PR에는 `docs-bot` 라벨을 붙인다.

PR 본문은 참조 PR, 수정·신규·삭제 요약, 아키텍처 검토, 문서 없는 클래스 skeleton, 수동 확인 항목을 포함한다.

---

## 8. 한계와 수동 검토

- AI 설정이 없고 삭제 매핑이 있으면 `_print_summary()`의 `functionn_info` 오타로 `AttributeError`가 발생한다.
- AI 설정이 없고 제외 문서 매핑이 있으면 `_print_summary()`가 존재하지 않는 `FunctionInfo.doc_file_path`를 조회해 `AttributeError`가 발생한다. 현재 정상 요약 종료는 두 매핑 목록이 모두 비어 있을 때만 보장된다.
- 경량 정규식 fallback은 지역 변수, initializer, 복잡한 macro를 함수로 오탐할 수 있다.
- 오버로드와 같은 이름의 private/public 함수가 같은 문서 섹션에 매핑될 수 있다.
- 구현 의도와 아키텍처 결정은 코드만으로 확정할 수 없다.
- 함수가 여러 개념 문서에 흩어져 있으면 최대 3개 섹션 제한으로 일부 참조가 빠질 수 있다.
- 문서가 전혀 없는 클래스는 자동 파일 생성 대상이 아니므로 반드시 사람이 생성 여부와 위치를 결정해야 한다.
- 문서 수정 PR은 코드 owner가 시그니처, 상태 전제 조건, 스레드 안전성, 예외·오류 의미를 최종 검토해야 한다.

---

## 관련 문서

- `Docs/style_guide.md` - 생성·수정 문서 형식
- `Docs/Testing.md` - PR CI와 docs-bot 운영 위치
- `.github/workflows/docs-bot.yml` - 실제 trigger와 환경 변수 전달
