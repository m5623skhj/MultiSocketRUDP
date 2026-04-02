"""
AI 클라이언트 모듈.
문서-코드 비교 분석 및 신규 문서 생성을 위한 AI API 추상화 레이어.
모델 교체 가능 구조 (Claude / OpenAI / Gemini 등).
"""

import json
import time
import logging
from abc import ABC, abstractmethod
from dataclasses import dataclass, field

from code_extractor import FunctionInfo
from doc_mapper import DocSection, SectionGroup

logger = logging.getLogger(__name__)


# ============================================================
# 분석 결과 데이터 클래스
# ============================================================
@dataclass
class AnalysisResult:
    """AI 비교 분석 결과."""
    needs_update: bool = False           # 문서 수정이 필요한가
    updated_content: str = ""            # 수정된 문서 섹션 내용
    reason: str = ""                     # 수정 판단 이유
    is_error: bool = False               # AI 호출 실패 여부
    error_message: str = ""              # 실패 메시지
    function_names: list[str] = field(default_factory=list)  # 관련 함수명들


@dataclass
class GenerateResult:
    """AI 신규 문서 생성 결과."""
    content: str = ""                    # 생성된 문서 내용
    is_error: bool = False
    error_message: str = ""
    function_name: str = ""


# ============================================================
# 프롬프트 빌더
# ============================================================
class PromptBuilder:
    """AI 프롬프트를 구성한다."""

    @staticmethod
    def build_system_prompt(style_guide: str) -> str:
        """시스템 프롬프트를 구성한다."""
        return f"""당신은 소프트웨어 기능 문서의 정합성을 검증하고 수정하는 전문가입니다.

역할:
- 코드의 인터페이스(public API)와 문서가 일치하는지 비교 분석합니다.
- 내부 구현 변경은 무시하고, 호출자 관점에서 알아야 할 정보만 판단합니다.
- 문서 수정 시 기존 문서의 스타일, 톤, 구조를 최대한 유지합니다.

판단 기준 (인터페이스 관점):
- 함수 시그니처 (반환 타입, 파라미터, 이름) 변경
- 사전/사후 조건 변경
- 에러 및 예외 케이스 변경
- 반환값의 의미 변경

무시해야 하는 변경:
- 내부 구현 알고리즘 변경 (외부 동작이 동일한 경우)
- 내부 변수명 변경
- 성능 최적화 (인터페이스 변경 없는 경우)

출력 규칙:
- 마크다운 형식으로 작성합니다.
- 기존 문서에 있는 옵시디언 위키링크 ([[문서명]], ![[파일명.svg]])는 반드시 보존합니다.
- 코드 블록 내 시그니처는 최신 코드와 일치하도록 갱신합니다.

=== 스타일 가이드 ===
{style_guide if style_guide else "(스타일 가이드 없음 - 기존 문서의 스타일을 따르세요)"}
"""

    @staticmethod
    def build_compare_prompt(
        function_infos: list[FunctionInfo],
        section: DocSection,
    ) -> str:
        """비교 분석용 유저 프롬프트를 구성한다."""
        # 변경된 함수 정보 정리
        functions_desc = []
        for info in function_infos:
            desc = f"### 함수: {info.class_name}::{info.name}\n"
            desc += f"- 변경 유형: {info.change_type}\n"
            if info.signature:
                desc += f"- 시그니처: `{info.signature}`\n"
            if info.return_type:
                desc += f"- 반환 타입: {info.return_type}\n"
            if info.parameters:
                params = ", ".join(f"{p['type']} {p['name']}" for p in info.parameters)
                desc += f"- 파라미터: {params}\n"
            if info.access_modifier:
                desc += f"- 접근 제한자: {info.access_modifier}\n"
            if info.qualifiers:
                desc += f"- 한정자: {', '.join(info.qualifiers)}\n"
            if info.doc_comment:
                desc += f"- 코드 주석:\n```\n{info.doc_comment}\n```\n"
            if info.raw_code:
                desc += f"- 코드 원본:\n```\n{info.raw_code[:1500]}\n```\n"
            functions_desc.append(desc)

        functions_block = "\n".join(functions_desc)  # 추가: f-string 외부에서 join 처리
        return f"""다음 함수의 최신 코드와 현재 문서 섹션을 비교하여 문서 수정이 필요한지 판단하세요.

## 변경된 함수 정보

{functions_block}

## 현재 문서 섹션

```markdown
{section.content}
```

## 요청

1. 위 함수의 최신 코드와 현재 문서 섹션을 **인터페이스 관점에서만** 비교하세요.
2. 문서 수정이 필요하면 `needs_update: true`와 수정된 전체 섹션을 반환하세요.
3. 수정이 불필요하면 `needs_update: false`와 이유를 반환하세요.
4. 수정 시 변경되지 않은 부분은 원본 그대로 유지하세요.
5. 옵시디언 위키링크([[...]], ![[...]])는 반드시 보존하세요.

다음 JSON 형식으로만 응답하세요:
```json
{{
  "needs_update": true/false,
  "reason": "판단 이유",
  "updated_content": "수정된 전체 섹션 내용 (needs_update가 true일 때만)"
}}
```
"""

    @staticmethod
    def build_generate_prompt(
        function_info: FunctionInfo,
        nearby_section: str = "",
    ) -> str:
        """신규 함수 문서 생성용 유저 프롬프트를 구성한다."""
        desc = f"### 함수: {function_info.class_name}::{function_info.name}\n"
        if function_info.signature:
            desc += f"- 시그니처: `{function_info.signature}`\n"
        if function_info.return_type:
            desc += f"- 반환 타입: {function_info.return_type}\n"
        if function_info.parameters:
            params = ", ".join(f"{p['type']} {p['name']}" for p in function_info.parameters)
            desc += f"- 파라미터: {params}\n"
        if function_info.access_modifier:
            desc += f"- 접근 제한자: {function_info.access_modifier}\n"
        if function_info.qualifiers:
            desc += f"- 한정자: {', '.join(function_info.qualifiers)}\n"
        if function_info.doc_comment:
            desc += f"- 코드 주석:\n```\n{function_info.doc_comment}\n```\n"
        if function_info.raw_code:
            desc += f"- 코드 원본:\n```\n{function_info.raw_code[:1500]}\n```\n"

        nearby_ref = ""
        if nearby_section:
            nearby_ref = f"""
## 참고: 기존 문서의 스타일 예시

다음은 같은 문서에 있는 다른 섹션입니다. 이 스타일을 참고하여 작성하세요:

```markdown
{nearby_section[:2000]}
```
"""

        return f"""다음 신규 함수에 대한 문서 섹션을 생성하세요.

## 함수 정보

{desc}

{nearby_ref}

## 요청

1. 인터페이스 관점에서 호출자가 알아야 할 정보를 문서화하세요:
   - 함수 설명
   - 시그니처 (코드 블록)
   - 파라미터 설명
   - 반환값 설명
   - 에러/예외 케이스 (해당 시)
   - 사전/사후 조건 (해당 시)
2. 마크다운 형식으로 작성하세요.
3. 기존 문서 스타일을 따르세요.

마크다운 섹션 내용만 반환하세요 (JSON 아님, 순수 마크다운):
"""


# ============================================================
# AI 클라이언트 추상 인터페이스
# ============================================================
class AIClient(ABC):
    """AI API 추상 인터페이스. 모델별 구현체를 교체 가능."""

    @abstractmethod
    def _call_api(self, system_prompt: str, user_prompt: str) -> str:
        """AI API를 호출하여 응답 텍스트를 반환한다."""
        ...

    def compare_and_suggest(
        self,
        section_group: SectionGroup,
        style_guide: str = "",
    ) -> AnalysisResult:
        """
        최종 코드 vs 현재 문서 섹션을 비교 분석한다.

        Args:
            section_group: 같은 섹션 내 변경 함수 그룹.
            style_guide: 스타일 가이드 내용.

        Returns:
            AnalysisResult 객체.
        """
        system_prompt = PromptBuilder.build_system_prompt(style_guide)
        user_prompt = PromptBuilder.build_compare_prompt(
            section_group.function_infos,
            section_group.section,
        )

        try:
            response = self._call_api_with_retry(system_prompt, user_prompt)
            return self._parse_compare_response(response, section_group)
        except Exception as e:
            logger.error("AI 비교 분석 실패: %s", e)
            return AnalysisResult(
                is_error=True,
                error_message=str(e),
                function_names=[f.name for f in section_group.function_infos],
            )

    def generate_doc(
        self,
        function_info: FunctionInfo,
        style_guide: str = "",
        nearby_section: str = "",
    ) -> GenerateResult:
        """
        신규 함수에 대한 문서 초안을 생성한다.

        Args:
            function_info: 신규 함수 정보.
            style_guide: 스타일 가이드 내용.
            nearby_section: 같은 문서의 다른 섹션 (스타일 참고용).

        Returns:
            GenerateResult 객체.
        """
        system_prompt = PromptBuilder.build_system_prompt(style_guide)
        user_prompt = PromptBuilder.build_generate_prompt(function_info, nearby_section)

        try:
            response = self._call_api_with_retry(system_prompt, user_prompt)
            return GenerateResult(
                content=response.strip(),
                function_name=function_info.name,
            )
        except Exception as e:
            logger.error("AI 문서 생성 실패: %s", e)
            return GenerateResult(
                is_error=True,
                error_message=str(e),
                function_name=function_info.name,
            )

    def _call_api_with_retry(self, system_prompt: str, user_prompt: str) -> str:
        """재시도 로직을 포함한 API 호출."""
        max_retries = 3
        base_delay = 2

        for attempt in range(max_retries):
            try:
                return self._call_api(system_prompt, user_prompt)
            except RateLimitError:
                delay = base_delay * (2 ** attempt)
                logger.warning("Rate limit 도달, %d초 후 재시도 (attempt %d/%d)",
                               delay, attempt + 1, max_retries)
                time.sleep(delay)
            except TimeoutError:
                if attempt == 0:
                    logger.warning("Timeout, 1회 재시도")
                    continue
                raise
            except ServerError:
                if attempt < 2:
                    logger.warning("서버 에러, 재시도 (attempt %d/%d)", attempt + 1, max_retries)
                    time.sleep(base_delay)
                    continue
                raise

        raise Exception(f"AI API 호출 {max_retries}회 재시도 후 실패")

    @staticmethod
    def _parse_compare_response(response: str, section_group: SectionGroup) -> AnalysisResult:
        """AI 비교 분석 응답을 파싱한다."""
        func_names = [f.name for f in section_group.function_infos]

        # JSON 추출 (마크다운 코드 블록 제거)
        cleaned = response.strip()
        if cleaned.startswith("```json"):
            cleaned = cleaned[7:]
        elif cleaned.startswith("```"):
            cleaned = cleaned[3:]
        if cleaned.endswith("```"):
            cleaned = cleaned[:-3]
        cleaned = cleaned.strip()

        try:
            data = json.loads(cleaned)
            return AnalysisResult(
                needs_update=data.get("needs_update", False),
                updated_content=data.get("updated_content", ""),
                reason=data.get("reason", ""),
                function_names=func_names,
            )
        except json.JSONDecodeError as e:
            logger.warning("AI 응답 JSON 파싱 실패: %s", e)
            # 수정: 파싱 실패 시 needs_update=False로 안전하게 처리.
            # 기존 로직은 원본 응답 전체를 updated_content로 사용하여 문서를 손상시킬 수 있었음.
            # 에러로 기록하여 PR 본문에 수동 확인 요청으로 표시.
            return AnalysisResult(
                is_error=True,  # 수정: needs_update=True → is_error=True
                error_message=f"AI 응답 JSON 파싱 실패: {e}",
                function_names=func_names,
            )


# ============================================================
# 커스텀 예외
# ============================================================
class RateLimitError(Exception):
    pass


class TimeoutError(Exception):
    pass


class ServerError(Exception):
    pass


# ============================================================
# Claude API 구현체
# ============================================================
class ClaudeClient(AIClient):
    """Anthropic Claude API 구현체."""

    def __init__(self, api_key: str, model: str = "claude-sonnet-4-20250514"):
        self.api_key = api_key
        self.model = model
        self.api_url = "https://api.anthropic.com/v1/messages"

    def _call_api(self, system_prompt: str, user_prompt: str) -> str:
        import requests

        headers = {
            "x-api-key": self.api_key,
            "content-type": "application/json",
            "anthropic-version": "2023-06-01",
        }

        payload = {
            "model": self.model,
            "max_tokens": 4096,
            "system": system_prompt,
            "messages": [{"role": "user", "content": user_prompt}],
        }

        try:
            r = requests.post(self.api_url, headers=headers, json=payload, timeout=60)
        except requests.Timeout:
            raise TimeoutError("Claude API timeout")

        if r.status_code == 429:
            raise RateLimitError("Claude API rate limit")
        if r.status_code >= 500:
            raise ServerError(f"Claude API server error: {r.status_code}")
        if r.status_code != 200:
            raise Exception(f"Claude API error {r.status_code}: {r.text[:200]}")

        data = r.json()
        return data["content"][0]["text"]


# ============================================================
# OpenAI API 구현체
# ============================================================
class OpenAIClient(AIClient):
    """OpenAI API 구현체."""

    def __init__(self, api_key: str, model: str = "gpt-4o"):
        self.api_key = api_key
        self.model = model
        self.api_url = "https://api.openai.com/v1/chat/completions"

    def _call_api(self, system_prompt: str, user_prompt: str) -> str:
        import requests

        headers = {
            "Authorization": f"Bearer {self.api_key}",
            "Content-Type": "application/json",
        }

        payload = {
            "model": self.model,
            "messages": [
                {"role": "system", "content": system_prompt},
                {"role": "user", "content": user_prompt},
            ],
            "max_tokens": 4096,
        }

        try:
            r = requests.post(self.api_url, headers=headers, json=payload, timeout=60)
        except requests.Timeout:
            raise TimeoutError("OpenAI API timeout")

        if r.status_code == 429:
            raise RateLimitError("OpenAI API rate limit")
        if r.status_code >= 500:
            raise ServerError(f"OpenAI API server error: {r.status_code}")
        if r.status_code != 200:
            raise Exception(f"OpenAI API error {r.status_code}: {r.text[:200]}")

        data = r.json()
        return data["choices"][0]["message"]["content"]


# ============================================================
# Gemini API 구현체
# ============================================================
class GeminiClient(AIClient):
    """Google Gemini API 구현체."""

    def __init__(self, api_key: str, model: str = "gemini-2.5-flash"):
        self.api_key = api_key
        self.model = model
        self.api_url = f"https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent"

    def _call_api(self, system_prompt: str, user_prompt: str) -> str:
        import requests

        headers = {"Content-Type": "application/json"}
        params = {"key": self.api_key}

        payload = {
            "system_instruction": {"parts": [{"text": system_prompt}]},
            "contents": [{"parts": [{"text": user_prompt}]}],
        }

        try:
            r = requests.post(self.api_url, headers=headers, params=params, json=payload, timeout=60)
        except requests.Timeout:
            raise TimeoutError("Gemini API timeout")

        if r.status_code == 429:
            raise RateLimitError("Gemini API rate limit")
        if r.status_code >= 500:
            raise ServerError(f"Gemini API server error: {r.status_code}")
        if r.status_code != 200:
            raise Exception(f"Gemini API error {r.status_code}: {r.text[:200]}")

        data = r.json()
        candidates = data.get("candidates", [])
        if not candidates:
            raise Exception("Gemini API: 응답에 candidates 없음")

        parts = candidates[0].get("content", {}).get("parts", [])
        return "".join(p.get("text", "") for p in parts)


# ============================================================
# 팩토리 함수
# ============================================================
def create_client(provider: str, api_key: str, model: str = "") -> AIClient:
    """
    AI 클라이언트를 생성한다.

    Args:
        provider: "claude" / "openai" / "gemini"
        api_key: API 키.
        model: 모델명 (비어 있으면 기본값 사용).

    Returns:
        AIClient 구현체.
    """
    provider = provider.lower()

    if provider == "claude":
        return ClaudeClient(api_key, model or "claude-sonnet-4-20250514")
    elif provider == "openai":
        return OpenAIClient(api_key, model or "gpt-4o")
    elif provider == "gemini":
        return GeminiClient(api_key, model or "gemini-2.5-flash")
    else:
        raise ValueError(f"지원하지 않는 AI 프로바이더: {provider}")


# ============================================================
# 전체 분석 실행
# ============================================================
def analyze_all(
    client: AIClient,
    section_groups: list[SectionGroup],
    added_infos: list[FunctionInfo],
    style_guide: str = "",
    docs_files: dict[str, str] | None = None,
) -> tuple[list[AnalysisResult], list[GenerateResult]]:
    """
    전체 변경에 대해 AI 분석을 수행한다.

    Args:
        client: AIClient 구현체.
        section_groups: 섹션 그룹 (기존 함수 수정 분석용).
        added_infos: 신규 추가된 함수 (문서 생성용).
        style_guide: 스타일 가이드 내용.
        docs_files: 문서 파일 딕셔너리 (스타일 참고용).

    Returns:
        (AnalysisResult 리스트, GenerateResult 리스트) 튜플.
    """
    analysis_results: list[AnalysisResult] = []
    generate_results: list[GenerateResult] = []

    # 1. 기존 문서 섹션 비교 분석
    for i, group in enumerate(section_groups):
        logger.info(
            "AI 비교 분석 [%d/%d]: %s — %s",
            i + 1, len(section_groups),
            group.doc_file_path,
            group.section.heading[:50],
        )

        result = client.compare_and_suggest(group, style_guide)
        analysis_results.append(result)

        if result.is_error:
            logger.error("  → 실패: %s", result.error_message)
        elif result.needs_update:
            logger.info("  → 수정 필요: %s", result.reason[:80])
        else:
            logger.info("  → 수정 불필요: %s", result.reason[:80])

    # 2. 신규 함수 문서 생성
    for i, info in enumerate(added_infos):
        logger.info(
            "AI 문서 생성 [%d/%d]: %s::%s",
            i + 1, len(added_infos), info.class_name, info.name,
        )

        # 기존 문서에서 스타일 참고용 섹션 가져오기
        nearby = ""
        if docs_files:
            for doc_path, content in docs_files.items():
                if info.class_name.lower() in doc_path.lower() and content:
                    sections = content.split("\n## ")
                    if len(sections) > 1:
                        nearby = "## " + sections[1][:2000]
                    break

        result = client.generate_doc(info, style_guide, nearby)
        generate_results.append(result)

        if result.is_error:
            logger.error("  → 실패: %s", result.error_message)
        else:
            logger.info("  → 생성 완료 (%d자)", len(result.content))

    # 요약
    analysis_ok = sum(1 for r in analysis_results if not r.is_error)
    analysis_update = sum(1 for r in analysis_results if r.needs_update and not r.is_error)
    analysis_fail = sum(1 for r in analysis_results if r.is_error)
    generate_ok = sum(1 for r in generate_results if not r.is_error)
    generate_fail = sum(1 for r in generate_results if r.is_error)

    logger.info(
        "AI 분석 완료: 비교 %d건 (수정필요 %d, 실패 %d), 생성 %d건 (실패 %d)",
        analysis_ok, analysis_update, analysis_fail, generate_ok, generate_fail,
    )

    return analysis_results, generate_results
