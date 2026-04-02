"""
문서 수정 모듈.
AI 분석 결과를 실제 문서 파일에 반영한다.

주요 기능:
- 기존 섹션 수정 (AI 응답으로 교체)
- 신규 섹션 추가 ('관련 문서' 앞에 삽입)
- 삭제된 함수 섹션 제거
- 옵시디언 위키링크/frontmatter 보존
- 목차 갱신
"""

import re
import logging
from dataclasses import dataclass, field

from doc_mapper import DocSection, DocMapping, parse_document_sections
from ai_client import AnalysisResult, GenerateResult
from code_extractor import FunctionInfo

logger = logging.getLogger(__name__)


# ============================================================
# 섹션 수정
# ============================================================
def update_section(doc_content: str, section: DocSection, new_content: str) -> str:
    """문서에서 특정 섹션을 새 내용으로 교체한다."""
    lines = doc_content.splitlines(keepends=True)

    original_links = _extract_wiki_links(section.content)
    new_links = _extract_wiki_links(new_content)
    _warn_missing_links(original_links, new_links)

    preserved = new_content
    if preserved and not preserved.endswith("\n"):
        preserved += "\n"

    new_lines = preserved.splitlines(keepends=True)
    result_lines = lines[:section.start_line] + new_lines + lines[section.end_line:]
    return "".join(result_lines)


# ============================================================
# 섹션 추가
# ============================================================
def add_section(doc_content: str, new_section_content: str) -> str:
    """'## 관련 문서' 앞에 새 섹션을 삽입한다. 없으면 문서 끝에 추가."""
    lines = doc_content.splitlines(keepends=True)
    section_text = "\n---\n\n" + new_section_content.strip() + "\n\n"

    related_line = _find_related_docs_line(lines)
    if related_line is not None:
        insert_at = related_line
        if insert_at > 0 and lines[insert_at - 1].strip() == "---":
            insert_at -= 1
        while insert_at > 0 and lines[insert_at - 1].strip() == "":
            insert_at -= 1

        new_lines = section_text.splitlines(keepends=True)
        result_lines = lines[:insert_at + 1] + new_lines + lines[insert_at + 1:]
        return "".join(result_lines)

    return doc_content.rstrip() + section_text


# ============================================================
# 섹션 삭제
# ============================================================
def remove_section(doc_content: str, section: DocSection) -> str:
    """문서에서 특정 섹션을 제거한다. 앞의 구분선과 빈 줄도 함께 제거."""
    lines = doc_content.splitlines(keepends=True)
    start = section.start_line
    end = section.end_line

    if start > 0 and lines[start - 1].strip() == "---":
        start -= 1
    while start > 0 and lines[start - 1].strip() == "":
        start -= 1

    result_lines = lines[:start] + lines[end:]
    return "".join(result_lines)


# ============================================================
# 목차 갱신
# ============================================================
def update_toc(doc_content: str) -> str:
    """문서의 '## 목차' 섹션을 현재 헤딩 구조에 맞게 갱신한다."""
    lines = doc_content.splitlines()

    toc_start = None
    toc_end = None
    for i, line in enumerate(lines):
        if line.strip() == "## 목차":
            toc_start = i
        elif toc_start is not None and toc_end is None:
            if line.strip().startswith("## ") or line.strip() == "---":
                toc_end = i
                break

    if toc_start is None:
        return doc_content
    if toc_end is None:
        toc_end = len(lines)

    headings = []
    for i, line in enumerate(lines):
        if i <= toc_start:
            continue
        match = re.match(r"^## (\d+)\.\s+(.+)$", line.strip())
        if match:
            num = match.group(1)
            title = match.group(2).strip()
            anchor = _heading_to_anchor(f"{num}. {title}")
            headings.append(f"{num}. [{title}](#{anchor})")

    if not headings:
        return doc_content

    new_toc = ["## 목차", ""] + headings + [""]
    result_lines = lines[:toc_start] + new_toc + lines[toc_end:]
    return "\n".join(result_lines)


# ============================================================
# 전체 수정 실행
# ============================================================
def apply_all_changes(
    docs_files: dict[str, str],
    analysis_results: list[AnalysisResult],
    generate_results: list[GenerateResult],
    deleted_mappings: list[DocMapping],
    section_groups: list,
    mappings: list[DocMapping],
) -> dict[str, str]:
    """
    AI 분석 결과를 문서에 반영하고 변경된 파일 딕셔너리를 반환한다.

    Returns:
        {파일 경로: 수정된 내용} (변경된 파일만).
    """
    working: dict[str, str] = {}
    change_log: list[str] = []

    def get(path):
        if path not in working:
            working[path] = docs_files.get(path, "")
        return working[path]

    # 1. 섹션 수정
    for result, group in zip(analysis_results, section_groups):
        if result.is_error or not result.needs_update:
            continue

        fp = group.doc_file_path
        content = get(fp)
        if not content:
            continue

        sections = parse_document_sections(content)
        target = _find_matching_section(sections, group.section.heading)
        if not target:
            logger.warning("섹션 못 찾음: %s in %s", group.section.heading[:40], fp)
            continue

        working[fp] = update_section(content, target, result.updated_content)
        funcs = ", ".join(result.function_names)
        change_log.append(f"수정: {fp} [{target.heading[:40]}] ({funcs})")
        logger.info("섹션 수정: %s [%s]", fp, target.heading[:40])

    # 2. 삭제
    for mapping in deleted_mappings:
        if not mapping.sections or not mapping.doc_file_path:
            continue
        fp = mapping.doc_file_path
        content = get(fp)
        if not content:
            continue

        for section in mapping.sections:
            sections = parse_document_sections(content)
            target = _find_matching_section(sections, section.heading)
            if target:
                content = remove_section(content, target)
                change_log.append(f"삭제: {fp} [{section.heading[:40]}]")
                logger.info("섹션 삭제: %s [%s]", fp, section.heading[:40])

        working[fp] = content

    # 3. 신규 추가
    for result in generate_results:
        if result.is_error or not result.content:
            continue

        target_file = _find_doc_for_function(result.function_name, mappings)
        if not target_file:
            change_log.append(f"생성 보류: {result.function_name} (대응 문서 없음)")
            continue

        content = get(target_file)
        working[target_file] = add_section(content, result.content)
        change_log.append(f"추가: {target_file} ({result.function_name})")
        logger.info("신규 추가: %s (%s)", target_file, result.function_name)

    # 4. 변경 파일 수집 + 목차 갱신 + 최종 검증
    file_changes: dict[str, str] = {}
    for fp, updated in working.items():
        original = docs_files.get(fp, "")
        if updated != original:
            final = update_toc(updated)
            final = _final_obsidian_check(original, final)
            file_changes[fp] = final

    logger.info("문서 수정 완료: %d개 파일", len(file_changes))
    for entry in change_log:
        logger.info("  %s", entry)

    return file_changes


# ============================================================
# PR 본문 생성
# ============================================================
def build_pr_body(
    analysis_results, generate_results, deleted_mappings,
    excluded_mappings, no_doc_mappings, no_section_mappings,
    source_prs,
) -> str:
    """문서 수정 PR의 본문을 마크다운으로 생성한다."""
    parts = ["## 📝 문서 자동 갱신\n\n이 PR은 docs-bot이 자동 생성했습니다.\n\n"]

    if source_prs:
        parts.append("### 참조 PR\n")
        for pr in source_prs:
            parts.append(f"- #{pr.number} {pr.title}\n")
        parts.append("\n")

    updates = [r for r in analysis_results if r.needs_update and not r.is_error]
    if updates:
        parts.append("### ✏️ 수정\n")
        for r in updates:
            funcs = ", ".join(f"`{n}`" for n in r.function_names)
            parts.append(f"- {funcs}: {r.reason}\n")
        parts.append("\n")

    gen_ok = [r for r in generate_results if not r.is_error]
    if gen_ok:
        parts.append("### ✨ 신규 생성\n")
        for r in gen_ok:
            parts.append(f"- `{r.function_name}`\n")
        parts.append("\n")

    if deleted_mappings:
        parts.append("### 🗑️ 삭제\n")
        for m in deleted_mappings:
            parts.append(f"- `{m.function_info.class_name}::{m.function_info.name}`\n")
        parts.append("\n")

    if excluded_mappings:
        parts.append("### ⚠️ 아키텍처 문서 검토 필요\n")
        for m in excluded_mappings:
            parts.append(f"- `{m.function_info.name}` → {m.doc_file_path}\n")
        parts.append("\n")

    errors = [r for r in analysis_results if r.is_error]
    gen_errs = [r for r in generate_results if r.is_error]
    if errors or gen_errs or no_doc_mappings or no_section_mappings:
        parts.append("### ❌ 수동 확인 필요\n")
        for r in errors:
            parts.append(f"- AI 분석 실패: {', '.join(r.function_names)}\n")
        for r in gen_errs:
            parts.append(f"- 문서 생성 실패: `{r.function_name}`\n")
        for m in no_doc_mappings:
            parts.append(f"- 매핑 실패(문서없음): `{m.function_info.name}`\n")
        for m in no_section_mappings:
            parts.append(f"- 매핑 실패(섹션없음): `{m.function_info.name}`\n")

    parts.append("\n---\n*Generated by docs-bot*\n")
    return "".join(parts)


# ============================================================
# 헬퍼
# ============================================================
def _extract_wiki_links(content: str) -> set[str]:
    return set(re.findall(r"!?\[\[([^\]]+)\]\]", content))

def _warn_missing_links(original: set[str], updated: set[str]):
    for link in original - updated:
        logger.warning("위키링크 누락: [[%s]]", link)

def _find_related_docs_line(lines) -> int | None:
    for i, line in enumerate(lines):
        if line.strip() == "## 관련 문서":
            return i
    return None

def _heading_to_anchor(heading: str) -> str:
    anchor = heading.lower()
    anchor = re.sub(r"[^\w\s가-힣-]", "", anchor)
    anchor = re.sub(r"\s+", "-", anchor.strip())
    return anchor

def _find_matching_section(sections, target_heading) -> DocSection | None:
    for s in sections:
        if s.heading == target_heading:
            return s
    target_clean = re.sub(r"^\d+[\.\-]\s*", "", target_heading).strip()
    for s in sections:
        if re.sub(r"^\d+[\.\-]\s*", "", s.heading).strip() == target_clean:
            return s
    return None

def _find_doc_for_function(function_name, mappings) -> str | None:
    for m in mappings:
        if m.function_info.name == function_name and m.doc_file_path:
            return m.doc_file_path
    return None

def _final_obsidian_check(original: str, updated: str) -> str:
    if original.startswith("---\n"):
        end = original.find("\n---\n", 4)
        if end != -1 and not updated.startswith("---\n"):
            updated = original[:end + 5] + updated
    updated = re.sub(r"\n{4,}", "\n\n\n", updated)
    if not updated.endswith("\n"):
        updated += "\n"
    return updated
