"""
문서 매퍼 모듈.
변경된 코드와 문서 파일/섹션을 매핑한다.

매핑 전략 (이름 기반, 2단계):
  1단계 — 파일 매핑: 코드 파일의 클래스명 → Docs/ 하위의 동일 이름 md 파일
  2단계 — 섹션 매핑: 변경된 함수명 → 문서 내 해당 함수가 언급된 섹션
"""

import os
import re
import logging
from dataclasses import dataclass, field

from config import CODE_TO_DOCS_DIR_MAP, DOCS_DIR, EXCLUDED_DOCS, CLASS_TO_DOC_OVERRIDE  # 수정: CLASS_TO_DOC_OVERRIDE 추가
from code_extractor import FunctionInfo

logger = logging.getLogger(__name__)


# ============================================================
# 데이터 클래스
# ============================================================
@dataclass
class DocSection:
    """문서 내 하나의 섹션."""
    heading: str            # 섹션 헤딩 텍스트 (예: "## 5. 패킷 송신 API")
    heading_level: int      # 헤딩 레벨 (2, 3, 4)
    start_line: int         # 시작 라인 (0-based)
    end_line: int           # 끝 라인 (0-based, exclusive)
    content: str            # 섹션 전체 내용


@dataclass
class DocMapping:
    """코드 변경과 문서의 매핑 결과."""
    function_info: FunctionInfo
    doc_file_path: str | None = None          # 대응하는 문서 파일 경로
    sections: list[DocSection] = field(default_factory=list)  # 관련 섹션들
    mapping_status: str = "success"           # success / no_doc / no_section / excluded
    message: str = ""                         # 추가 정보 (실패 사유 등)


# ============================================================
# 문서 파일 탐색 캐시
# ============================================================
_docs_file_cache: dict[str, str] | None = None


def _build_docs_file_index(docs_dir_content: dict[str, str]) -> dict[str, str]:
    """
    Docs/ 하위의 모든 md 파일을 클래스명 → 파일 경로로 인덱싱한다.

    Args:
        docs_dir_content: {파일 경로: 파일 내용} 딕셔너리 (Docs/ 하위 전체).

    Returns:
        {클래스명(소문자): 파일 경로} 딕셔너리.
    """
    index = {}
    for file_path in docs_dir_content:
        if not file_path.endswith(".md"):
            continue
        basename = os.path.basename(file_path)
        name = os.path.splitext(basename)[0]
        index[name.lower()] = file_path
    return index


# ============================================================
# 1단계: 파일 매핑
# ============================================================
def find_related_doc(
    function_info: FunctionInfo,
    docs_files: dict[str, str],
) -> str | None:
    """
    변경된 함수의 클래스명을 기반으로 대응하는 문서 파일을 찾는다.

    매핑 방법:
    1. 코드 파일 경로에서 디렉토리를 매핑 테이블로 변환
    2. 클래스명과 동일한 이름의 md 파일을 탐색

    Args:
        function_info: 변경된 함수 정보.
        docs_files: {문서 파일 경로: 내용} 딕셔너리.

    Returns:
        대응하는 문서 파일 경로. 없으면 None.
    """
    class_name = function_info.class_name
    if not class_name:
        return None

    # 추가: 수동 매핑 테이블 우선 조회 (클래스명과 문서 파일명이 불일치하는 경우)
    override_path = CLASS_TO_DOC_OVERRIDE.get(class_name.lower())
    if override_path and override_path in docs_files:
        return override_path

    # 매핑 테이블 기반 탐색
    code_path = function_info.file_path
    target_docs_dir = None

    for code_dir, docs_dir in CODE_TO_DOCS_DIR_MAP.items():
        if code_path.startswith(code_dir):
            target_docs_dir = docs_dir
            break

    # 1차: 매핑된 문서 디렉토리에서 클래스명.md 탐색
    if target_docs_dir:
        for doc_path in docs_files:
            if not doc_path.startswith(target_docs_dir):
                continue
            basename = os.path.splitext(os.path.basename(doc_path))[0]
            if basename.lower() == class_name.lower():
                return doc_path

        # BotTester는 하위 디렉토리가 깊으므로 재귀 탐색
        if "BotTester" in target_docs_dir:
            for doc_path in docs_files:
                if not doc_path.startswith(target_docs_dir):
                    continue
                basename = os.path.splitext(os.path.basename(doc_path))[0]
                # C# 파일명과 문서명 대응 (예: GeminiClient.cs → GeminiClient.md)
                if basename.lower() == class_name.lower():
                    return doc_path
                # _CS 접미사 처리 (예: RudpSession_CS.md)
                if basename.lower().replace("_cs", "") == class_name.lower():
                    return doc_path

    # 2차: 전체 Docs 디렉토리에서 이름 매칭 (폴백)
    for doc_path in docs_files:
        basename = os.path.splitext(os.path.basename(doc_path))[0]
        if basename.lower() == class_name.lower():
            return doc_path

    return None


# ============================================================
# 2단계: 섹션 매핑
# ============================================================
def parse_document_sections(content: str) -> list[DocSection]:
    """
    마크다운 문서를 헤딩 단위로 섹션 분리한다.

    ## (h2), ### (h3), #### (h4) 레벨을 인식한다.

    Args:
        content: 문서 전체 내용.

    Returns:
        DocSection 리스트.
    """
    lines = content.splitlines()
    sections: list[DocSection] = []
    heading_pattern = re.compile(r"^(#{2,4})\s+(.+)$")

    heading_positions = []
    for i, line in enumerate(lines):
        match = heading_pattern.match(line)
        if match:
            level = len(match.group(1))
            heading_text = match.group(2).strip()
            heading_positions.append((i, level, heading_text))

    for idx, (start, level, heading) in enumerate(heading_positions):
        # 다음 동일 또는 상위 레벨 헤딩까지가 이 섹션의 범위
        if idx + 1 < len(heading_positions):
            end = heading_positions[idx + 1][0]
        else:
            end = len(lines)

        section_content = "\n".join(lines[start:end])
        sections.append(DocSection(
            heading=heading,
            heading_level=level,
            start_line=start,
            end_line=end,
            content=section_content,
        ))

    return sections


def find_related_sections(
    function_name: str,
    sections: list[DocSection],
) -> list[DocSection]:
    """
    문서 섹션 중 변경된 함수명이 언급된 섹션을 찾는다.

    탐색 우선순위:
    1. 헤딩에 함수명이 직접 포함된 섹션 (예: "## 5. 패킷 송신 API — SendPacket")
    2. 본문에 함수 시그니처가 포함된 섹션 (예: 코드 블록 내 `bool SendPacket(`)
    3. 본문에 함수명이 언급된 섹션

    Args:
        function_name: 찾을 함수명.
        sections: parse_document_sections()의 결과.

    Returns:
        관련 섹션 리스트 (우선순위 순).
    """
    if function_name == "*":
        # 파일 전체 삭제 시 모든 섹션 반환
        return sections

    # 함수명 이스케이프 (정규식 특수문자 처리)
    escaped_name = re.escape(function_name)

    # 헤딩에 함수명 포함
    heading_matches = []
    # 본문에 시그니처 패턴 포함
    signature_matches = []
    # 본문에 함수명 언급
    mention_matches = []

    for section in sections:
        # "목차" 또는 "관련 문서" 섹션은 스킵
        if section.heading in ("목차", "관련 문서"):
            continue

        # 1. 헤딩 매칭
        if re.search(escaped_name, section.heading, re.IGNORECASE):
            heading_matches.append(section)
            continue

        # 백틱 내 함수명도 체크 (예: `RegisterTimerEvent`)
        if f"`{function_name}`" in section.heading or f"`{function_name}(" in section.heading:
            heading_matches.append(section)
            continue

        # 2. 코드 블록 내 시그니처 패턴
        sig_pattern = rf"{escaped_name}\s*\("
        if re.search(sig_pattern, section.content):
            # 코드 블록 내에 있는지 확인
            in_code_block = False
            for line in section.content.splitlines():
                if line.strip().startswith("```"):
                    in_code_block = not in_code_block
                if in_code_block and re.search(sig_pattern, line):
                    signature_matches.append(section)
                    break

            if section not in signature_matches:
                mention_matches.append(section)
            continue

        # 3. 본문 언급
        if function_name in section.content:
            mention_matches.append(section)

    # 우선순위 기반 반환: 상위 매칭이 있으면 하위는 제외하여 노이즈 감소
    # 헤딩 매칭이 있으면 그것만 반환 (가장 직접적인 섹션)
    # 없으면 시그니처 매칭, 그래도 없으면 언급 매칭
    if heading_matches:
        primary = heading_matches
    elif signature_matches:
        primary = signature_matches
    else:
        primary = mention_matches

    # 중복 제거 및 최대 3개로 제한 (AI 토큰 관리)
    result = []
    seen = set()
    for section in primary:
        key = (section.start_line, section.heading)
        if key not in seen:
            seen.add(key)
            result.append(section)
        if len(result) >= 3:
            break

    return result


# ============================================================
# 제외 문서 확인
# ============================================================
def is_excluded_doc(doc_path: str) -> bool:
    """아키텍처/가이드 문서인지 확인한다."""
    return doc_path in EXCLUDED_DOCS


# ============================================================
# 메인 매핑 함수
# ============================================================
def map_all(
    function_infos: list[FunctionInfo],
    docs_files: dict[str, str],
) -> list[DocMapping]:
    """
    변경된 함수 전체에 대해 문서 매핑을 수행한다.

    Args:
        function_infos: code_extractor의 결과.
        docs_files: {문서 파일 경로: 내용} 딕셔너리.

    Returns:
        DocMapping 리스트.
    """
    mappings: list[DocMapping] = []

    for info in function_infos:
        mapping = DocMapping(function_info=info)

        # 1단계: 파일 매핑
        doc_path = find_related_doc(info, docs_files)

        if not doc_path:
            if info.change_type == "added":
                mapping.mapping_status = "no_doc"
                mapping.message = f"신규 함수 '{info.class_name}::{info.name}'에 대응하는 문서 없음 → 문서 추가 대상"
            else:
                mapping.mapping_status = "no_doc"
                mapping.message = f"'{info.class_name}::{info.name}'에 대응하는 문서를 찾지 못함"
            logger.warning(mapping.message)
            mappings.append(mapping)
            continue

        # 제외 문서 확인
        if is_excluded_doc(doc_path):
            mapping.doc_file_path = doc_path
            mapping.mapping_status = "excluded"
            mapping.message = f"아키텍처 문서 '{doc_path}' → 자동화 대상 제외, 수동 검토 필요"
            logger.info(mapping.message)
            mappings.append(mapping)
            continue

        mapping.doc_file_path = doc_path

        # 2단계: 섹션 매핑
        doc_content = docs_files.get(doc_path, "")
        if doc_content:
            sections = parse_document_sections(doc_content)
            related_sections = find_related_sections(info.name, sections)

            if related_sections:
                mapping.sections = related_sections
                mapping.mapping_status = "success"
                logger.info(
                    "매핑 성공: %s::%s → %s (%d개 섹션)",
                    info.class_name, info.name, doc_path, len(related_sections),
                )
            else:
                mapping.mapping_status = "no_section"
                mapping.message = (
                    f"문서 '{doc_path}'에서 '{info.name}' 관련 섹션을 찾지 못함"
                )
                logger.warning(mapping.message)
        else:
            mapping.mapping_status = "no_section"
            mapping.message = f"문서 '{doc_path}'의 내용을 읽을 수 없음"

        mappings.append(mapping)

    # 요약 로그
    success = sum(1 for m in mappings if m.mapping_status == "success")
    no_doc = sum(1 for m in mappings if m.mapping_status == "no_doc")
    no_section = sum(1 for m in mappings if m.mapping_status == "no_section")
    excluded = sum(1 for m in mappings if m.mapping_status == "excluded")

    logger.info(
        "문서 매핑 완료: 성공 %d, 문서 없음 %d, 섹션 없음 %d, 제외 %d",
        success, no_doc, no_section, excluded,
    )

    return mappings


# ============================================================
# 같은 문서 + 같은 섹션 내 변경을 그룹핑
# ============================================================
@dataclass
class SectionGroup:
    """AI에 한 번에 전달할 섹션 단위 그룹."""
    doc_file_path: str
    section: DocSection
    function_infos: list[FunctionInfo] = field(default_factory=list)
    mappings: list[DocMapping] = field(default_factory=list)


def group_by_section(mappings: list[DocMapping]) -> list[SectionGroup]:
    """
    같은 문서의 같은 섹션에 속한 변경을 묶어서 AI 호출을 최소화한다.

    Args:
        mappings: map_all()의 결과 (성공 항목만).

    Returns:
        SectionGroup 리스트.
    """
    groups: dict[tuple[str, int], SectionGroup] = {}

    for mapping in mappings:
        if mapping.mapping_status != "success":
            continue

        for section in mapping.sections:
            key = (mapping.doc_file_path, section.start_line)

            if key not in groups:
                groups[key] = SectionGroup(
                    doc_file_path=mapping.doc_file_path,
                    section=section,
                )

            groups[key].function_infos.append(mapping.function_info)
            groups[key].mappings.append(mapping)

    result = list(groups.values())
    logger.info("섹션 그룹핑: %d개 그룹 (AI 호출 횟수)", len(result))
    return result
