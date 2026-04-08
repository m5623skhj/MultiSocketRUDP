# docs-bot - 전체 기능 및 구조 서술

## 1. 프로젝트 개요

PR 머지 시 변경된 코드의 인터페이스(함수, 시그니처, 파라미터, 반환값, 에러 케이스)를 감지하고, 기존 기능 문서와 비교하여 불일치를 AI가 판단한 뒤, 문서 수정 PR을 자동 생성하는 GitHub Actions 기반 CI 파이프라인.

"문서를 자동으로 관리하는 시스템"이 아닌, **"사람이 놓치지 않도록 잡아주는 보조 시스템"**으로 설계되었다.

---

## 2. 자동화 범위

### 대상
- 기능(인터페이스) 레벨 문서만 한정
- 캡슐화 관점: 호출자가 알아야 할 정보만 문서화, 내부 구현은 제외
- 함수 시그니처, 파라미터, 반환값, 동작 설명, 사전/사후 조건, 에러 및 예외 케이스

### 변경 유형별 처리
- **함수 수정**: 기존 문서 섹션과 최신 코드를 AI가 비교하여 수정 제안
- **함수 추가**: AI가 문서 초안 생성
- **함수 삭제**: 해당 문서 섹션 자동 제거 (AI 호출 불필요)

### 대상 제외
- 아키텍처 레벨 문서 (config.py의 `EXCLUDED_DOCS`로 관리)
- 내부 구현 상세 (C++ .cpp만 변경되고 .h 미변경인 경우 자동 제외)
- 생성자/소멸자

---

## 3. 파일 구조

```
.github/workflows/docs-bot.yml          워크플로우 정의
    main.py                             파이프라인 오케스트레이션
    config.py                           설정, 매핑 테이블, 제외 목록
    git_utils.py                        GitHub API 유틸리티
    change_detector.py                  변경 감지
    code_extractor.py                   코드 추출
    doc_mapper.py                       문서 매핑
    ai_client.py                        AI API 추상화
    doc_writer.py                       문서 수정 + PR 본문 생성
    requirements.txt                    의존성
    __init__.py
```

---

## 4. 파이프라인 흐름

```
매일 00:00 UTC
    Step 1: DOCS_BOT_LAST_RUN 조회
        없으면 현재 시간 설정 후 종료(초기 실행)
    
    Step 2: 머지된 PR 수집
        없으면 즉시 종료
    
    Step 3: 기존 봇 PR 자동 닫기

    Step 4: 변경 감지 + 코드 추출
        변경된 .h/.hpp/.cs 파일에서 함수 감지
        내부 구현 변경 필터링 (.cpp만 수정 시 제외)
        생성자/소멸자 필터링
        main 브랜치 최종 코드에서 시그니처/파라미터/반환 타입 추출

    Step 5: 문서 매핑
        1. 수동 매핑 테이블 -> 클래스명 기반 -> fallback 전체 검색
        2. 헤딩에 함수명이 포함된 섹션 탐색 (최대 3개)
        아키텍처 문서 -> 플래그만 표시
        같은 섹션 내 변경을 그룹핑 (AI 호출 최소화)

    Step 6: AI 분석
        기존 함수: 최종 코드 vs 문서 섹션 비교 -> 수정 필요 여부
        신규 함수: 문서 초안 생성
        삭제 함수: AI 호출 없이 섹션 제거
        재시도: rate limit 지수 백오프 3회, timeout 1회, 서버 에러 2회

    Step 7: 문서 수정
        섹션 교체/추가/삭제
        목차 자동 갱신
        옵시디언 위키 링크 보존 검증

    Step 8: 브랜치 생성 -> 커밋 -> PR 생성
        PR 본문: 수정/생성/삭제 요약, 참조 PR, AI 판단 근거
        아키텍처 문서 검토 필요 플래그
        문서 없는 클래스 -> 스켈레톤 제안 (시그니처 기반)
        실패 항목 목록
    
    Step 9: DOCS_BOT_LAST_RUN 갱신 (성공 시에만)
```

---

## 5. 모듈별 기능 상세

### 5.1. main.py - 파이프라인 오케스트레이션

전체 8단계 흐름을 순차적으로 실행한다. 각 단계의 실패 시 적절한 종료 코드를 반환하고, GitHub Actions output 변수를 설정하여 워크플로우의 후속 step에서 조건 분기에 사용한다.

주요 분기:
- 초기 실행 (DOCS_BOT_LAST_RUN 없음) -> 시간 설정 후 정상 종료
- 머지된 PR 없음 -> 즉시 종료
- 인터페이스 변경 없음 -> 즉시 종료
- AI 미설정 -> 결과 요약만 출력하고 정상 종료, DOCS_BOT_LAST_RUN 미갱신
- 문서 변경 없음 -> PR 미생성

### 5.2. config.py - 설정 관리

설정 : 내용
`CODE_tO_DOCS_DIR_MAP` : 코드 디렉토리 -> 문서 디렉토리 매핑
`CLASS_TO_DOC_OVDERRIDE` : 클래스명 <-> 문서 파일명 불일치 수동 매핑
`EXCLUDED_DOCS` : 아키텍처 / 가이드 문서 제외 목록
`TARGET_EXTENSIONS` : 감지 대상 확장자 (.h, .hpp, .cpp, .cc, .cs)
`AI_PROVIDER`, `AI_API_KEY`, `AI_MODEL` : AI API 설정

### 5-3. git_utils.py - GitHub API 유틸리티

함수 : 역할
`get_last_run_time()` : DOCS_BOT_LAST_RUN 파싱 (초기 실행 시 None 반환)
`get_merged_prs()` : 기간 내 머지된 PR 조회 (봇 PR 제외)
`close_existing_bot_prs()` : 열려 있는 봇 PR 자동 닫기
`create_branch()` : main 최신 커밋에서 새 브랜치 생성
`commit_and_push()` : GitHub API tree/commit 방식으로 파일 커밋
`create_pr()` : 문서 수정 PR 새엇ㅇ + docs-bot 라벨 추가
`get_file_content()` : main 브랜치 파일 내용 조회

### 5.4. change_detector.py - 변경 감지

PR diff에서 변경된 파일과 함수를 식별하고 변경 유형을 분류한다. 범위 축소가 목적이므로 경량 파싱(정규식)으로 처리

핵심 로직:
- **diff 파싱** : unified diff를 파일별 추가/삭제 라인 정보로 분해

### 5.5. code_extractor.py - 코드 추출

main 브랜치의 최종 코드에서 함수의 구조화된 정보를 추출한다.

추출 정보 : 시그니처, 반환 타입, 파라미터 (타입 + 이름), 접근 제한자, 한정자 (virtual, static, async, [[nodiscard]] 등), @brief/@param 주석, 코드 원본

파싱 방식:
- **tree-sitter (우선)**: C++/C# AST 기반으로 추출. tree-sitter 미설치 시 자동 비활성화
- **정규식 (fallback)**: tree-sitter 없을 때 동작, return_type, parameters, access_modifier 모두 시그니처 문자열에서 파싱

### 5-6. doc_mapper.py - 문서 매핑

변경된 코드와 문서 파일/섹션을 연결한다.

**1단계 - 파일 매핑**:
1. `CLASS_TO_DOC_OVERRIDE` 수동 매핑 테이블 조회
2. `CODE_TO_DOCS_DIR_MAP`으로 코드 디렉토리 -> 문서 디렉토리 변환 후 클래스명 매칭
3. 전체 Docs/ 디렉토리에서 클래스명 매칭 (fallback)

**2단계 - 섹셩 매핑**:
1. 헤딩에 함수명이 직접 포함된 섹션
2. 코드 블록 내 함수 시그니처가 포함된 섹션
3. 본문에 함수명이 언급된 섹션

상위 우선순위 매칭이 있으면 하위는 제외, 최대 3개 섹션으로 제한하여 토큰 비용 최소화

**센션 그룹핑**: 같은 문서의 같은 섹션에 속한 변경을 묶어서 AI 호출 1회로 처리

### 5.7. ai_client.py - AI API 추상화

모델 교체 가능 구조, `AIClient` 추상 클래스를 상속하여 구현체 제공

구현체 / 모델 기본값
`ClaudeClient` / claude-sonnet-4-20250514
`OpenAIClient` / gpt-4o
`GeminiClient` / gemini-2.5-flash

`create_client("gemini", api_key)` 방식으로 사용

프롬프트 설계:
- **시스템 프롬프트**: 역할 정의 + 인터페이스 관점 판단 기준 + 스타일 가이드
- **비교 분석 프롬프트**: 최종 함수 코드 + 현재 문서 섹션 -> JSON 응답(needs_update + updated_content + reason)
- **신규 생성 프롬프트**: 함수 정보 + 기존 문서 스타일 참고 -> 마크다운 섹션

재시도 로직:
- rate limit : 지수 백오프 2초 -> 4초 -> 8초, 최대 3회
- timeout: 1회 재시도 후 스킵
- 서버 에러: 2회 재시도 후 스킵
- JSON 파싱 실패ㅣ is_error=True로 처리하여 문서 손상 방지

### 5-8. doc_writer.py - 문서 수정 + PR 본문 생성

**문서 수정 함수**

함수 / 역할
`update_section()` / 기존 섹션을 AI 응답으로 교체 + 위키링크 손실 경고
`add_section()` / "## 관련 문서" 앞에 새 섹션 삽입
`remove_section()` / 삭제된 함수의 섹션 제거 (앞쪽 구분선/빈줄 정리)
`update_toc()` / "## 목차" 섹션을 현재 헤딩 구조에 맞게 재성성
`apply_all_chages()` / 위 함수들을 조합하여 파일별 변경 적용 (삭제 -> 수정 -> 추가 순서)

**PR 본문 구성**
- 참조 PR 목록
- ✏️ 수정 : 함수명 + AI 판단 근거
- ✨ 신규 생성 : 함수명
- 🗑️ 삭제 : 클래스명::함수명
- ⚠️ 아키텍처 문서 검토 필요 : 플래그
- 📋 문서 없는 클래스 : 클래스별 skeleton 제안 (시그니처/파라미터/반환 타입 기반, `<details>` 접힘)
- ❌ 수동 확인 필요 : AI 분석 실패, 섹션 매핑 실패 항목

**옵시디언 호환성**
- 위키링크 (`[[문서명]]`, `![[파일명.svg]]`) 보존 검증
- frontmatter 보존
- 연속 빈 줄 정리(4줄 이상 -> 3줄)

### 5-9. docs-bot.yml - GitHub Actions 워크플로우

트리거:
- `schedule: cron "0 0 * * *"` (매일 00:00 UTC)
- `pull_request: labeled` : (docs-reviw 라벨)
- `workflow_dispatch` : (수동)

환경 변수
- `GITHUB_TOKEN` : 자동 제공
- `DOCS_BOT_LAST_RUN` : Repository Variable에서 읽기
- `AI_PROVIDER`, `AI_MODEL` : Repository Variables
- `AI_API_KEY` : Repository Secret

DOCS_BOT_LAST_RUN 갱신 조건 :
- 초기 실행이 아님
- AI 스킵이 아님 (AI 키 추가 전 변경 누락 방지)
- PR 생성 송공
- Job 성공

---

## 6. 설계 결정 요약
항목 / 결정  / 이유
자동화 범위 / 인터페이스 문서만 / 아키텍처 문서는 AI 판단이 어려워 보이고, 기술 부채 리스크가 존재
트리거 / 매일 00:00 스케줄링 / 매 PR 호출 시 AI 토큰 비용이 부담됨
diff 기준 / main 브랜치 최종 코드 상태 / 오버로드, 중복 변경 방지
문서 매핑 / 클래스명 기반 + 수동 override / frontmatter 불필요, 기존 프로젝트 구조 활용
섹션 매핑 / 헤딩 우선 + 최대 3개 / 노이즈 감소 및 토큰 절약
AI 응답 파싱 실패 / is_error=True (문서 미적용) / 원본 응답을 문서에 적용하면 손상 위험
AI 미설정 시 / 정상 종료 + LAST_RUN 미갱신 / 키 추가 후 이전 변경 재처리 유도
기본 봇 PR / 새 실행 시 자동 닫기 / main 기준 최신 상태만 유효
기존 문서 정합성 / 완전하다는 전제 / 매번 검증 시 토큰 소모량이 극히 부담됨
봇 역할 / 감지 + 제안 / AI 판단 한계 보완

---

## 7. 한계

- AI가 "함수가 무엇을 하는지"는 파악 가능하나, "왜 이렇게 만들었는지"는 코드만으로 파악하기 어려움
- 기능 문서에 한정해도 아키텍처적 맥락이 스며들 수 있음(인터페이스 관점 한정으로 최소화)
- 신규 함수 문서 초안은 의도/사용 맥락이 부정확 할 수 있음 -> 리뷰 단계에서 보정
- 동일 클래스 내 private 오버로드와 public 오버로드가 같은 이름일 때, 변경 감지는 되지만 문서 매핑 시 같은 섹션으로 연결됨
